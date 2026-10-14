/* Coral-Wave USB bring-up. USB transport runs independently of codec MCLK.
 * Playback PCM is drained; capture is silence until the I2S bridge is added.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "sam.h"
#include "tusb.h"
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

typedef struct {
  uint32_t magic, version, stage, error, millis, mounted, mount_count;
  uint32_t playback_active, capture_active, playback_bytes, capture_bytes;
  uint32_t control_requests, codec_ack, suspend_count;
} status_t;
static volatile status_t *const status = (volatile status_t *)0x20007000;
static volatile uint32_t ticks;
static uint8_t mute[2][3];
static int16_t volume[2][3];
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;
void Reset_Handler(void);
void HardFault_Handler(void);
void SysTick_Handler(void);
void USB_Handler(void);
__attribute__((section(".vectors"), used))
const uintptr_t vectors[48] = {
  [0] = 0x20008000, [1] = (uintptr_t)Reset_Handler,
  [2] = (uintptr_t)HardFault_Handler, [3] = (uintptr_t)HardFault_Handler,
  [15] = (uintptr_t)SysTick_Handler, [16 + USB_IRQn] = (uintptr_t)USB_Handler
};
static void fail(uint32_t error) {
  status->error = error; for (;;) __NOP();
}
void HardFault_Handler(void) { fail(9); }
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
void tud_umount_cb(void) { status->mounted = status->playback_active = status->capture_active = 0; }
void tud_suspend_cb(bool remote) { (void)remote; ++status->suspend_count; }
void tud_resume_cb(void) { status->mounted = tud_mounted(); }
bool tud_audio_set_itf_cb(uint8_t rhport, const tusb_control_request_t *request) {
  (void)rhport;
  unsigned itf = tu_u16_low(request->wIndex), alt = tu_u16_low(request->wValue);
  if (itf == 1) status->playback_active = alt != 0;
  if (itf == 2) status->capture_active = alt != 0;
  return true;
}
bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, const tusb_control_request_t *request) {
  return tud_audio_set_itf_cb(rhport, request);
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
  return (entity == 2 || entity == 0x12) && *channel < (*path ? 2u : 3u);
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
    case AUDIO10_CS_REQ_GET_RES: value = 256; break;
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
    mute[path][channel] = buffer[0] != 0; return true;
  }
  if (selector == AUDIO10_FU_CTRL_VOLUME && request->wLength == 2) {
    int16_t value = (int16_t)tu_unaligned_read16(buffer);
    if (value > 0 || value < -90 * 256) return false;
    volume[path][channel] = value; return true;
  }
  return false;
}
static void main_loop(void) {
  uint32_t last_frame = ticks;
  uint8_t playback[196];
  const int16_t silence[48] = { 0 };
  for (;;) {
    tud_task();
    if (tud_audio_available()) status->playback_bytes += tud_audio_read(playback, sizeof(playback));
    if (ticks != last_frame) {
      last_frame = ticks;
      if (status->capture_active) status->capture_bytes += tud_audio_write(silence, sizeof(silence));
    }
  }
}
void Reset_Handler(void) {
  __disable_irq(); SCB->VTOR = (uint32_t)vectors;
  uint32_t *source = &_sidata;
  for (uint32_t *p = &_sdata; p < &_edata; ++p) *p = *source++;
  for (uint32_t *p = &_sbss; p < &_ebss; ++p) *p = 0;
  memset((void *)status, 0, sizeof(*status));
  status->magic = 0x434F524C; status->version = 1; status->stage = 1;
  board_init(); status->stage = 2;
  __enable_irq();
  const tusb_rhport_init_t init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
  if (!tusb_init(0, &init)) fail(3);
  status->stage = 3; main_loop();
}
