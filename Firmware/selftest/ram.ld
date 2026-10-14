ENTRY(Reset_Handler)
MEMORY { RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 28K }
SECTIONS {
  .vectors : { KEEP(*(.vectors)) } > RAM
  .text : { *(.text*) *(.rodata*) } > RAM
  .data : { *(.data*) } > RAM
  .bss (NOLOAD) : {
    . = ALIGN(16); __bss_start__ = .;
    *(.bss*) *(COMMON)
    . = ALIGN(16); __bss_end__ = .;
  } > RAM
  /DISCARD/ : { *(.comment) *(.ARM.exidx*) *(.ARM.extab*) }
  ASSERT(__bss_end__ <= 0x20006F00, "Self-test overlaps result mailbox")
}
