/* Coral-Wave full-duplex USB Audio Class 1.0 sound card. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "sam.h"
#include "tusb.h"
#include "audio.h"
#if CORAL_USB_DEBUG
#include <stdarg.h>
#include <stdio.h>
volatile char usb_trace[4096];
volatile unsigned usb_trace_index;
/* Integer-only debug logging needs no heap. Reject any libc allocation. */
void *_sbrk(int increment) { (void)increment; return (void *)-1; }
int coral_usb_log(const char *format, ...) {
  char line[128];
  va_list args;
  va_start(args, format);
  int n = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  for (int i = 0; i < n && i < (int)sizeof(line) - 1; ++i)
    usb_trace[usb_trace_index++ % sizeof(usb_trace)] = line[i];
  return n;
}
#endif

volatile audio_status_t *const status = (volatile audio_status_t *)0x20007000;
static volatile uint32_t ticks;
static uint8_t mute[2][3];
static int16_t volume[2][3];
static bool suspended_playback, suspended_capture;
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;
void Reset_Handler(void);
void HardFault_Handler(void);
void SysTick_Handler(void);
void USB_Handler(void);
__attribute__((section(".vectors"), used))
const uintptr_t vectors[48] = {
  [0] = 0x20008000, [1] = (uintptr_t)Reset_Handler,
  [2] = (uintptr_t)HardFault_Handler, [3] = (uintptr_t)HardFault_Handler,
  [15] = (uintptr_t)SysTick_Handler, [16 + USB_IRQn] = (uintptr_t)USB_Handler,
  [16 + DMAC_IRQn] = (uintptr_t)DMAC_Handler
};
static void fail(uint32_t error) {
  status->error = error; for (;;) __NOP();
}
__attribute__((used, noreturn)) void hard_fault_capture(const uint32_t *frame, uint32_t exception_return) {
  status->fault_pc = frame[6]; status->fault_lr = frame[5];
  status->fault_xpsr = frame[7]; status->fault_exception_return = exception_return;
  NVIC_DisableIRQ(DMAC_IRQn);
  if (PM->AHBMASK.reg & PM_AHBMASK_DMAC) DMAC->CTRL.reg = 0;
  fail(9);
  for (;;) { }
}
__attribute__((naked)) void HardFault_Handler(void) {
  __asm volatile("movs r0, #4\n mov r1, lr\n tst r0, r1\n beq 1f\n mrs r0, psp\n b 2f\n"
                 "1: mrs r0, msp\n 2: mov r1, lr\n b hard_fault_capture\n");
}
void SysTick_Handler(void) { ++ticks; status->millis = ticks; }
void USB_Handler(void) { tud_int_handler(0); }
uint32_t tusb_time_millis_api(void) { return ticks; }
void tusb_time_delay_ms_api(uint32_t ms) {
  uint32_t start = ticks; while (ticks - start < ms) __NOP();
}
static void wait_gclk(void) {
  uint32_t limit = 1000000;
  while (GCLK->STATUS.bit.SYNCBUSY) if (--limit == 0) fail(1);
}
static void wait_sysctrl(uint32_t mask) {
  uint32_t limit = 2000000;
  while ((SYSCTRL->PCLKSR.reg & mask) != mask) if (--limit == 0) fail(2);
}
static void board_init(void) {
  NVMCTRL->CTRLB.bit.RWS = 1;
  SYSCTRL->OSC8M.bit.PRESC = 0;
  GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(0) | GCLK_GENCTRL_SRC_OSC8M | GCLK_GENCTRL_GENEN;
  wait_gclk();
  /* PA18 enables Y1 and Y2. Y1 12 MHz is the USB reference. */
  PORT->Group[0].OUTSET.reg = 1u << 18;
  PORT->Group[0].DIRSET.reg = 1u << 18;
  for (volatile unsigned i = 0; i < 20000; ++i) __NOP();
  SYSCTRL->XOSC.reg = SYSCTRL_XOSC_ENABLE | SYSCTRL_XOSC_GAIN_3 | SYSCTRL_XOSC_STARTUP(2);
  wait_sysctrl(SYSCTRL_PCLKSR_XOSCRDY);
  GCLK->GENDIV.reg = GCLK_GENDIV_ID(1) | GCLK_GENDIV_DIV(375);
  wait_gclk();
  GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(1) | GCLK_GENCTRL_SRC_XOSC | GCLK_GENCTRL_GENEN;
  wait_gclk();
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_DFLL48 | GCLK_CLKCTRL_GEN_GCLK1 | GCLK_CLKCTRL_CLKEN;
  wait_gclk();
  wait_sysctrl(SYSCTRL_PCLKSR_DFLLRDY);
  SYSCTRL->DFLLCTRL.reg = SYSCTRL_DFLLCTRL_ENABLE;
  wait_sysctrl(SYSCTRL_PCLKSR_DFLLRDY);
  uint32_t coarse = (*(const uint32_t *)0x00806024 >> 26) & 63;
  if (coarse == 63) coarse = 31;
  SYSCTRL->DFLLVAL.reg = SYSCTRL_DFLLVAL_COARSE(coarse) | SYSCTRL_DFLLVAL_FINE(512);
  wait_sysctrl(SYSCTRL_PCLKSR_DFLLRDY);
  SYSCTRL->DFLLMUL.reg = SYSCTRL_DFLLMUL_MUL(1500) | SYSCTRL_DFLLMUL_CSTEP(31) | SYSCTRL_DFLLMUL_FSTEP(511);
  wait_sysctrl(SYSCTRL_PCLKSR_DFLLRDY);
  SYSCTRL->DFLLCTRL.reg = SYSCTRL_DFLLCTRL_ENABLE | SYSCTRL_DFLLCTRL_MODE | SYSCTRL_DFLLCTRL_WAITLOCK;
  wait_sysctrl(SYSCTRL_PCLKSR_DFLLLCKC | SYSCTRL_PCLKSR_DFLLLCKF);
  GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(0) | GCLK_GENCTRL_SRC_DFLL48M | GCLK_GENCTRL_GENEN | GCLK_GENCTRL_IDC;
  wait_gclk();
  PM->AHBMASK.reg |= PM_AHBMASK_USB;
  PM->APBBMASK.reg |= PM_APBBMASK_USB;
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_USB | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN;
  wait_gclk();
  PORT->Group[0].PMUX[12].reg = PORT_PMUX_PMUXE_G | PORT_PMUX_PMUXO_G;
  PORT->Group[0].PINCFG[24].reg = PORT_PINCFG_PMUXEN;
  PORT->Group[0].PINCFG[25].reg = PORT_PINCFG_PMUXEN;
  SysTick_Config(48000);
}

void tud_mount_cb(void) { status->mounted = 1; ++status->mount_count; }
void tud_umount_cb(void) {
  status->mounted = 0; audio_stream(0, false); audio_stream(1, false);
  suspended_playback = suspended_capture = false;
}
void tud_suspend_cb(bool remote) {
  (void)remote; ++status->suspend_count;
  suspended_playback = status->playback_active; suspended_capture = status->capture_active;
  audio_stream(0, false); audio_stream(1, false);
}
void tud_resume_cb(void) {
  status->mounted = tud_mounted();
  audio_stream(0, suspended_playback); audio_stream(1, suspended_capture);
}
bool tud_audio_set_itf_cb(uint8_t rhport, const tusb_control_request_t *request) {
  (void)rhport;
  unsigned itf = tu_u16_low(request->wIndex), alt = tu_u16_low(request->wValue);
  if (itf == 1) { audio_stream(0, alt != 0); tud_audio_fb_set(48u << 16); }
  if (itf == 2) audio_stream(1, alt != 0);
  return true;
}
bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, const tusb_control_request_t *request) {
  return tud_audio_set_itf_cb(rhport, request);
}
void tud_audio_feedback_params_cb(uint8_t function, uint8_t alt, audio_feedback_params_t *params) {
  (void)function; (void)alt;
  params->method = AUDIO_FEEDBACK_METHOD_DISABLED; /* application FIFO controller */
  params->sample_freq = 48000;
}
bool tud_audio_get_req_ep_cb(uint8_t rhport, const tusb_control_request_t *request) {
  ++status->control_requests;
  if (tu_u16_high(request->wValue) != AUDIO10_EP_CTRL_SAMPLING_FREQ ||
      request->bRequest != AUDIO10_CS_REQ_GET_CUR) return false;
  uint8_t frequency[3] = { 0x80, 0xBB, 0x00 };
  return tud_audio_buffer_and_schedule_control_xfer(rhport, request, frequency, 3);
}
bool tud_audio_set_req_ep_cb(uint8_t rhport, const tusb_control_request_t *request, uint8_t *buffer) {
  (void)rhport; ++status->control_requests;
  return tu_u16_high(request->wValue) == AUDIO10_EP_CTRL_SAMPLING_FREQ &&
    request->bRequest == AUDIO10_CS_REQ_SET_CUR && request->wLength == 3 &&
    buffer[0] == 0x80 && buffer[1] == 0xBB && buffer[2] == 0;
}
static bool feature(const tusb_control_request_t *request, unsigned *path, unsigned *channel) {
  uint8_t entity = tu_u16_high(request->wIndex);
  *path = entity == 0x12; *channel = tu_u16_low(request->wValue);
  return (entity == 2 || entity == 0x12) && *channel == 0;
}
bool tud_audio_get_req_entity_cb(uint8_t rhport, const tusb_control_request_t *request) {
  ++status->control_requests;
  unsigned path, channel;
  if (!feature(request, &path, &channel)) return false;
  uint8_t selector = tu_u16_high(request->wValue);
  if (selector == AUDIO10_FU_CTRL_MUTE && request->bRequest == AUDIO10_CS_REQ_GET_CUR)
    return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &mute[path][channel], 1);
  if (selector != AUDIO10_FU_CTRL_VOLUME) return false;
  int16_t value;
  switch (request->bRequest) {
    case AUDIO10_CS_REQ_GET_CUR: value = volume[path][channel]; break;
    case AUDIO10_CS_REQ_GET_MIN: value = -90 * 256; break;
    case AUDIO10_CS_REQ_GET_MAX: value = 0; break;
    case AUDIO10_CS_REQ_GET_RES: value = 64; break;
    default: return false;
  }
  return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &value, sizeof(value));
}
bool tud_audio_set_req_entity_cb(uint8_t rhport, const tusb_control_request_t *request, uint8_t *buffer) {
  (void)rhport; ++status->control_requests;
  unsigned path, channel;
  if (!feature(request, &path, &channel) || request->bRequest != AUDIO10_CS_REQ_SET_CUR) return false;
  uint8_t selector = tu_u16_high(request->wValue);
  if (selector == AUDIO10_FU_CTRL_MUTE && request->wLength == 1) {
    mute[path][channel] = buffer[0] != 0;
    audio_controls(path, mute[path], volume[path]); return true;
  }
  if (selector == AUDIO10_FU_CTRL_VOLUME && request->wLength == 2) {
    int16_t value = (int16_t)tu_unaligned_read16(buffer);
    if (value > 0 || value < -90 * 256) return false;
    volume[path][channel] = value;
    audio_controls(path, mute[path], volume[path]); return true;
  }
  return false;
}
static void main_loop(void) { for (;;) { tud_task(); audio_task(); } }
void Reset_Handler(void) {
  __disable_irq(); SCB->VTOR = (uint32_t)vectors;
  uint32_t *source = &_sidata;
  for (uint32_t *p = &_sdata; p < &_edata; ++p) *p = *source++;
  for (uint32_t *p = &_sbss; p < &_ebss; ++p) *p = 0;
  memset((void *)status, 0, sizeof(*status));
  status->magic = 0x434F524C; status->version = 2; status->stage = 1;
  board_init(); status->stage = 2;
  __enable_irq();
  if (!audio_init()) fail(status->error ? status->error : 6);
  audio_controls(0, mute[0], volume[0]); audio_controls(1, mute[1], volume[1]);
  const tusb_rhport_init_t init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
  if (!tusb_init(0, &init)) fail(3);
  NVIC_SetPriority(USB_IRQn, 2);
  NVIC_SetPriority(SysTick_IRQn, 3);
  status->stage = 3; main_loop();
}
