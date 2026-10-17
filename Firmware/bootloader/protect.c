/* One-time SWD provisioning helper, executed exclusively from SRAM. */
#include "sam.h"
#include <stdint.h>
#include <stdbool.h>
#ifndef CW_BOOTPROTECTION
#define CW_BOOTPROTECTION 1
#endif
static void cmd(uint16_t value){NVMCTRL->CTRLA.reg=NVMCTRL_CTRLA_CMDEX_KEY|value;while(!NVMCTRL->INTFLAG.bit.READY){}}
__attribute__((used,noreturn)) void Reset_Handler(void){
 __disable_irq();SysTick->CTRL=0;NVIC->ICER[0]=0xFFFFFFFF;
 volatile uint32_t *result=(volatile uint32_t *)0x20007000;
 result[0]=0x43575052;result[1]=0;
 uint32_t page[16];const volatile uint32_t *source=(const volatile uint32_t *)0x00804000;
 for(unsigned i=0;i<16;++i)page[i]=source[i];
 page[0]=(page[0]&~7u)|CW_BOOTPROTECTION; /* DS40001882L table 22-2. */
 NVMCTRL->CTRLB.reg|=NVMCTRL_CTRLB_MANW|NVMCTRL_CTRLB_CACHEDIS;
 NVMCTRL->STATUS.reg=NVMCTRL_STATUS_PROGE|NVMCTRL_STATUS_LOCKE|NVMCTRL_STATUS_NVME;
 NVMCTRL->ADDR.reg=0x00804000/2;cmd(NVMCTRL_CTRLA_CMD_EAR);cmd(NVMCTRL_CTRLA_CMD_PBC);
 volatile uint32_t *dest=(volatile uint32_t *)0x00804000;
 for(unsigned i=0;i<16;++i)dest[i]=page[i];
 NVMCTRL->ADDR.reg=0x00804000/2;cmd(NVMCTRL_CTRLA_CMD_WAP);
 bool ok=!(NVMCTRL->STATUS.reg&(NVMCTRL_STATUS_PROGE|NVMCTRL_STATUS_LOCKE|NVMCTRL_STATUS_NVME));
 for(unsigned i=0;i<16;++i)if(dest[i]!=page[i])ok=false;
 result[1]=ok?1:2;result[2]=dest[0];for(;;)__NOP();
}
