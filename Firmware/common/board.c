#include "sam.h"
#include "board.h"
static void clock_fail(void) { for (;;) __NOP(); }
static void wait_gclk(void) {
  uint32_t limit = 1000000;
  while (GCLK->STATUS.bit.SYNCBUSY) if (--limit == 0) clock_fail();
}
static void wait_sysctrl(uint32_t mask) {
  uint32_t limit = 2000000;
  while ((SYSCTRL->PCLKSR.reg & mask) != mask) if (--limit == 0) clock_fail();
}
void board_init(void) {
  /* Reset below about 2.8 V so flash updates never continue through a brownout. */
  SYSCTRL->BOD33.bit.ENABLE=0;
  while(!SYSCTRL->PCLKSR.bit.B33SRDY) { }
  SYSCTRL->BOD33.reg=SYSCTRL_BOD33_LEVEL(39)|SYSCTRL_BOD33_HYST|SYSCTRL_BOD33_ACTION_NONE;
  SYSCTRL->BOD33.bit.ENABLE=1;
  while(!SYSCTRL->PCLKSR.bit.BOD33RDY || SYSCTRL->PCLKSR.bit.BOD33DET) { }
  SYSCTRL->BOD33.bit.ENABLE=0;
  while(!SYSCTRL->PCLKSR.bit.B33SRDY) { }
  SYSCTRL->BOD33.reg=SYSCTRL_BOD33_LEVEL(39)|SYSCTRL_BOD33_HYST|SYSCTRL_BOD33_ACTION_RESET|SYSCTRL_BOD33_ENABLE;
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
