/* Tracker+ simulator harness (harness.cpp): one flat RAM region, every section loaded where it runs (run.py maps it whole). Host tooling. */
MEMORY { RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 32M }
ENTRY(h_boot)
SECTIONS
{
  .text : { KEEP(*(.text.h_*)) KEEP(*(.text.h2_*)) *(.text*) *(.rodata*) *(.glue_7) *(.glue_7t) } > RAM
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > RAM
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > RAM
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > RAM
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > RAM
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > RAM
  .data : { *(.data*) } > RAM
  .bss (NOLOAD) : { __bss_start__ = .; *(.bss*) *(COMMON) __bss_end__ = .; } > RAM
  . = ALIGN(8);
  end = .;
  PROVIDE(_end = .);
  __StackTop = ORIGIN(RAM) + LENGTH(RAM);
  PROVIDE(__stack = __StackTop);
}
