#include "sam.h"
#include "tusb.h"
#include "audio.h"
#include "control.h"
#include "protocol.h"
#include <string.h>
static cw_params_t current, candidate;
static const cw_params_t defaults={.schema=1,.headphone=39,.mic_pga=40,.mic_boost=1,
 .hpf=1,.oversample=1,.eq_path=1,.eq_frequency={1,1,1,1,1},.limiter_level=2,
 .alc_target=11,.gate_threshold=3};
typedef struct { uint32_t magic, generation; cw_params_t params; uint32_t crc; } saved_t;
static uint32_t generation, saved_address, reset_at;
static struct { uint8_t reg; uint16_t value; } plan[24];
static unsigned plan_count, plan_index;
static bool applying, known=true;
static bool normal_reset;
static bool valid(const cw_params_t *p){
 if(p->schema!=1 || p->headphone>63 || p->mic_pga>63 || p->mic_boost>1 || p->hpf>2 ||
   p->hpf_cut>7 || p->oversample>1 || p->eq_path!=1 || p->eq_wide>7 || p->depth3d>15 ||
   p->limiter>1 || p->limiter_level>5 || p->alc>1 || p->alc_target>14 ||
   p->noise_gate>1 || p->gate_threshold>7 || p->dac_attenuation>48)return false;
 for(unsigned n=0;n<5;++n)if(p->eq_gain[n]<-12 || p->eq_gain[n]>12 || p->eq_frequency[n]>3)return false;
 for(unsigned n=0;n<sizeof(p->reserved);++n)if(p->reserved[n])return false;
 return !p->noise_gate || p->alc;
}
static void add(uint8_t reg,uint16_t value){
 if(audio_codec_shadow(reg)!=value || !known){plan[plan_count].reg=reg;plan[plan_count++].value=value;}
}
static void prepare(const cw_params_t *p){
 candidate=*p;plan_count=plan_index=0;
 /* EQ/3D is deliberately fixed to DAC. Changing its path requires stopping both ADC and DAC. */
 for(unsigned n=0;n<5;++n){
  uint16_t v=(12-p->eq_gain[n])|(p->eq_frequency[n]<<5);
  if(!n)v|=0x100;
  if(n>=1&&n<=3&&(p->eq_wide&(1u<<(n-1))))v|=0x100;
  add(18+n,v);
 }
 add(41,p->depth3d);add(24,0x032|(p->limiter?0x100:0));add(25,p->limiter_level<<4);
 add(11,255-p->dac_attenuation);add(12,0x100|(255-p->dac_attenuation));
 add(14,(p->oversample?8:0)|(p->hpf?0x100:0)|(p->hpf==2?(0x80|(p->hpf_cut<<4)):0));
 add(10,(audio_codec_shadow(10)&0x040)|(p->oversample?8:0));
 /* Disable ALC before changing PGA; enable left-only ALC with bounded gain after it. */
 if(p->alc!=current.alc || p->mic_pga!=current.mic_pga || !known)add(32,0x038);
 add(45,0x100|p->mic_pga);add(47,p->mic_boost?0x100:0);
 add(33,0x20|p->alc_target);add(34,0x032);
 add(35,(p->noise_gate?8:0)|p->gate_threshold);
 /* Min -12dB, max +23.25dB, left channel only. */
 plan[plan_count].reg=32;plan[plan_count++].value=(p->alc?0x100:0)|0x028;
 add(52,p->headphone);add(53,0x100|p->headphone);
 applying=true;
}
static bool step(void){
 if(plan_index<plan_count){
  unsigned n=plan_index++;
  if(!audio_codec_write(plan[n].reg,plan[n].value)){
   known=false;applying=false;return false;
  }
 }
 if(plan_index==plan_count){current=candidate;known=true;applying=false;}
 return true;
}
static bool saved_valid(const saved_t *s){
 return s->magic==0x43575053u && s->crc==cw_crc32(s,sizeof(*s)-4) && valid(&s->params);
}
void control_init(void){
 const saved_t *a=(const saved_t *)CW_SETTINGS_A,*b=(const saved_t *)CW_SETTINGS_B,*s=NULL;
 if(saved_valid(a))s=a;
 if(saved_valid(b)&&(!s || (int32_t)(b->generation-s->generation)>0))s=b;
 current=defaults;
 if(s){generation=s->generation;saved_address=(uint32_t)s;}
 known=false;prepare(s?&s->params:&defaults);
 while(applying){if(!step()){status->error=4;break;}}
}
void control_task(void){
 if(reset_at&&(int32_t)(status->millis-reset_at)>=0){
  if(normal_reset)NVIC_SystemReset();else cw_reset_to_boot();
 }
}
void cw_command(const cw_packet_t *q,cw_packet_t *r){
 r->result=CW_BAD_REQUEST;
 switch(q->command){
 case CW_INFO:if(q->length)return;cw_info(r,false);return;
 case CW_STATUS:{
  if(q->length)return;
  uint32_t s[]={status->millis,status->stage,status->error,status->codec_ack,status->lrclk_hz,
   status->playback_active,status->capture_active,status->playback_underflows,status->dma_errors,
   status->i2s_tx_underruns,status->i2s_rx_overruns,status->current_dac_peak,
   known};
  memcpy(r->data,s,sizeof(s));r->length=sizeof(s);r->result=CW_OK;return;
 }
 case CW_GET_PARAMS:
  if(q->length)return;
  if(!known){r->result=CW_CODEC_ERROR;return;}
  memcpy(r->data,&current,sizeof(current));r->length=sizeof(current);r->result=CW_OK;return;
 case CW_SET_PARAMS:
  if(q->length!=sizeof(current) || !valid((const cw_params_t *)q->data))return;
  if(!applying)prepare((const cw_params_t *)q->data);
  if(!step()){r->result=CW_CODEC_ERROR;return;}
  if(applying){r->result=CW_BUSY;return;}
  memcpy(r->data,&current,sizeof(current));r->length=sizeof(current);r->result=CW_OK;return;
 case CW_SAVE_PARAMS:{
  if(q->length)return;
  if(!known || applying){r->result=CW_BAD_STATE;return;}
  /* Never stall the real-time audio engine with a flash erase while streaming. */
  if(status->playback_active || status->capture_active){r->result=CW_BAD_STATE;return;}
  uint32_t address=saved_address==CW_SETTINGS_A?CW_SETTINGS_B:CW_SETTINGS_A;
  audio_quiesce();normal_reset=true;reset_at=status->millis+300;
  uint32_t page[16];memset(page,255,sizeof(page));
  saved_t s={.magic=0x43575053u,.generation=generation+1,.params=current};
  s.crc=cw_crc32(&s,sizeof(s)-4);memcpy(page,&s,sizeof(s));
  if(!cw_flash_erase(address)||!cw_flash_page(address,page)||!saved_valid((const saved_t *)address)){
   r->result=CW_FLASH_ERROR;return;
  }
  saved_address=address;generation=s.generation;r->result=CW_OK;return;
 }
 case CW_BOOT:if(q->length)return;reset_at=status->millis+300;r->result=CW_OK;return;
 case CW_REGISTERS:{
  if(q->length!=1 || q->data[0]>47)return;
  /* Diagnostic software mirror, not a WM8978 hardware readback. */
  for(unsigned n=0;n<10;++n){uint16_t v=audio_codec_shadow(q->data[0]+n);memcpy(r->data+n*2,&v,2);}
  r->length=20;r->result=CW_OK;return;
 }
 default:return;
 }
}
