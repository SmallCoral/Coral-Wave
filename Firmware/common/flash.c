#include "protocol.h"
#include "sam.h"
/* Only the application/settings region is writable through this module. The
 * bootloader and auxiliary fuse row are deliberately outside these bounds. */
static bool command(uint16_t cmd) {
  NVMCTRL->STATUS.reg = NVMCTRL_STATUS_PROGE | NVMCTRL_STATUS_LOCKE | NVMCTRL_STATUS_NVME;
  NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | cmd;
  while (!NVMCTRL->INTFLAG.bit.READY) { }
  return !(NVMCTRL->STATUS.reg & (NVMCTRL_STATUS_PROGE | NVMCTRL_STATUS_LOCKE | NVMCTRL_STATUS_NVME));
}
bool cw_flash_erase(uint32_t address) {
  if (address<CW_APP_BASE || address>0x3FF00u || (address&255)) return false;
  uint32_t irq=__get_PRIMASK(); __disable_irq();
  NVMCTRL->ADDR.reg=address/2;
  bool ok=command(NVMCTRL_CTRLA_CMD_ER);
  __set_PRIMASK(irq); return ok;
}
bool cw_flash_page(uint32_t address, const void *page) {
  if (address<CW_APP_BASE || address>0x3FFC0u || (address&63)) return false;
  uint32_t irq=__get_PRIMASK(); __disable_irq();
  NVMCTRL->CTRLB.bit.MANW=1;
  bool ok=command(NVMCTRL_CTRLA_CMD_PBC);
  const uint32_t *src=page; volatile uint32_t *dest=(volatile uint32_t *)address;
  if (ok) {
    for (unsigned i=0;i<16;++i) dest[i]=src[i];
    NVMCTRL->ADDR.reg=address/2; ok=command(NVMCTRL_CTRLA_CMD_WP);
    if (ok) for (unsigned i=0;i<16;++i) if (dest[i]!=src[i]) ok=false;
  }
  __set_PRIMASK(irq); return ok;
}
