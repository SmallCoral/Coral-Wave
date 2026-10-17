#include "sam.h"
#include "tusb.h"
#include "board.h"
#include "protocol.h"
#include <string.h>
extern uint32_t _sidata,_sdata,_edata,_sbss,_ebss;
static volatile uint32_t ticks;
static bool hold,writing;
static uint32_t received,length,expected_crc,image_version,run_at;
static uint8_t page[64] __attribute__((aligned(4)));
void Reset_Handler(void);
void SysTick_Handler(void){++ticks;}
void USB_Handler(void){tud_int_handler(0);}
void fault(void){for(;;){}}
__attribute__((section(".vectors"),used)) const uintptr_t vectors[48]={
 [0]=0x20008000,[1]=(uintptr_t)Reset_Handler,[2]=(uintptr_t)fault,[3]=(uintptr_t)fault,
 [15]=(uintptr_t)SysTick_Handler,[16+USB_IRQn]=(uintptr_t)USB_Handler
};
uint32_t tusb_time_millis_api(void){return ticks;}
void tusb_time_delay_ms_api(uint32_t ms){uint32_t t=ticks;while(ticks-t<ms){}}
static bool vectors_valid(uint32_t size){
 const uint32_t *v=(const uint32_t *)CW_APP_BASE;
 return v[0]>0x20000000u && v[0]<=0x20008000u && !(v[0]&7) && (v[1]&1) &&
   (v[1]&~1u)>=CW_APP_BASE && (v[1]&~1u)<CW_APP_BASE+size;
}
static bool valid_image(void){
 const cw_metadata_t *m=(const cw_metadata_t *)CW_META;
 return m->magic==0x57415645u && m->board==CW_BOARD && m->base==CW_APP_BASE &&
   m->length>=192 && m->length<=CW_APP_MAX && m->check==cw_crc32(m,24) &&
   vectors_valid(m->length) && m->crc==cw_crc32((const void *)CW_APP_BASE,m->length);
}
static uint32_t word(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
void cw_command(const cw_packet_t *q,cw_packet_t *r){
 r->result=CW_BAD_REQUEST;
 switch(q->command){
 case CW_INFO: if(q->length)return;cw_info(r,true);return;
 case CW_HOLD: if(q->length)return;hold=true;r->result=CW_OK;return;
 case CW_BEGIN:
  if(q->length!=16 || word(q->data)!=CW_BOARD)return;
  if(word(q->data+4)<192 || word(q->data+4)>CW_APP_MAX)return;
  length=word(q->data+4);expected_crc=word(q->data+8);image_version=word(q->data+12);
  hold=true;writing=false;received=0;run_at=0;
  if(!cw_flash_erase(CW_META)){r->result=CW_FLASH_ERROR;return;}
  memset(page,255,sizeof(page));writing=true;r->result=CW_OK;return;
 case CW_WRITE:{
  if(!writing){r->result=CW_BAD_STATE;return;}
  if(q->length<5 || q->length>52)return;
  uint32_t offset=word(q->data),count=q->length-4;
  if(offset!=received || received>length || length>CW_APP_MAX || count>length-received)return;
  for(unsigned n=0;n<count;++n){
   page[received&63]=q->data[n+4];++received;
   if(!(received&63)){
    uint32_t address=CW_APP_BASE+received-64;
    if((!(address&255)&&!cw_flash_erase(address)) || !cw_flash_page(address,page)){
     writing=false;r->result=CW_FLASH_ERROR;return;
    }
    memset(page,255,sizeof(page));
   }
  }
  memcpy(r->data,&received,4);r->length=4;r->result=CW_OK;return;
 }
 case CW_FINISH:{
  if(q->length)return;
  if(!writing || received!=length){r->result=CW_BAD_STATE;return;}
  writing=false;
  if(received&63){
   uint32_t address=CW_APP_BASE+(received&~63u);
   if((!(address&255)&&!cw_flash_erase(address)) || !cw_flash_page(address,page)){
    r->result=CW_FLASH_ERROR;return;
   }
  }
  if(!vectors_valid(length) || cw_crc32((const void *)CW_APP_BASE,length)!=expected_crc){r->result=CW_CRC_ERROR;return;}
  uint32_t metadata[16];memset(metadata,255,sizeof(metadata));
  cw_metadata_t m={0x57415645u,length,expected_crc,image_version,CW_BOARD,CW_APP_BASE,0};
  m.check=cw_crc32(&m,24);memcpy(metadata,&m,sizeof(m));
  if(!cw_flash_page(CW_META,metadata) || !valid_image()){r->result=CW_FLASH_ERROR;return;}
  memcpy(r->data,&m,sizeof(m));r->length=sizeof(m);r->result=CW_OK;return;
 }
 case CW_RUN:
  if(q->length)return;
  if(writing || !valid_image()){r->result=CW_BAD_STATE;return;}
  r->result=CW_OK;run_at=ticks+250;return;
 case CW_ABORT: if(q->length)return;writing=false;hold=true;run_at=0;r->result=CW_OK;return;
 default:return;
 }
}
__attribute__((naked,noreturn)) static void branch_to_app(uint32_t sp,uint32_t pc){
 (void)sp;(void)pc;__asm volatile("msr msp, r0\n bx r1");
}
static void start_app(void){
 tud_disconnect();tusb_time_delay_ms_api(30);tud_deinit(0);__disable_irq();
 SysTick->CTRL=0;SysTick->VAL=0;NVIC->ICER[0]=0xFFFFFFFFu;NVIC->ICPR[0]=0xFFFFFFFFu;
 SCB->ICSR=SCB_ICSR_PENDSTCLR_Msk|SCB_ICSR_PENDSVCLR_Msk;
 SCB->VTOR=CW_APP_BASE;__DSB();__ISB();
 const uint32_t *v=(const uint32_t *)CW_APP_BASE;branch_to_app(v[0],v[1]);
}
void Reset_Handler(void){
 __disable_irq();SCB->VTOR=(uint32_t)vectors;
 bool requested=CW_BOOT_KEY[0]==CW_BOOT_MAGIC && CW_BOOT_KEY[1]==~CW_BOOT_MAGIC;
 CW_BOOT_KEY[0]=CW_BOOT_KEY[1]=0;
 uint32_t *src=&_sidata;for(uint32_t *p=&_sdata;p<&_edata;++p)*p=*src++;
 for(uint32_t *p=&_sbss;p<&_ebss;++p)*p=0;
 hold=requested||!valid_image();board_init();__enable_irq();
 const tusb_rhport_init_t init={.role=TUSB_ROLE_DEVICE,.speed=TUSB_SPEED_FULL};tusb_init(0,&init);
 for(;;){tud_task();cw_hid_task();
  if(run_at && (int32_t)(ticks-run_at)>=0)start_app();
  if(!hold && ticks>2200)start_app();
 }
}
