ENTRY(Reset_Handler)
MEMORY {
  FLASH (rx) : ORIGIN = 0x00004000, LENGTH = 239K
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 28K
}
SECTIONS {
  .vectors : { KEEP(*(.vectors)) } > FLASH
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT>FLASH
  _sidata = LOADADDR(.data);
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  /DISCARD/ : { *(.comment) *(.ARM.extab*) }
  ASSERT(_ebss <= 0x20007000, "USB buffers overlap diagnostic mailbox")
}
