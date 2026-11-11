ENTRY(bench_run)
MEMORY
{
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 12M
  SDRAM (rwx) : ORIGIN = 0x70000000, LENGTH = 16M
}
SECTIONS
{
  .text : { *(.text*) *(.audio_ram_functions*) *(.audio_flash_functions*) *(.rodata*) } > RAM
  .init_array : { __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .; } > RAM
  .data : { *(.data*) } > RAM
  /* the SDRAM objects of the firmware: externalRamDelayBuff, externalRamReverbBuff and the reverb buffer E when it is not in SRAM */
  .sdram (NOLOAD) : { *(.sdram*) *(.noinit.$EXTERNAL_RAM*) } > SDRAM
  .bss : { *(.bss*) *(.dmabuffers*) *(COMMON) *(.noinit*) } > RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
