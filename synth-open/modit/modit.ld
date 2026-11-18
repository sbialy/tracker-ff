/* modit.plg (MOD import, IT import, IT export): linked at the image's SDRAM slot (MODULE_BASE, the image symbol s_ext) against the image's
   own symbols (ld --just-symbols), packed by synth-open/plug/pack_ext.py. The layout of the console module's script (the poly-devtools
   firmware/module/module.ld), nothing of it is shared:
   [base, __image_end)          text then data, what the file carries (the loader copies it)
   [__image_end, __bss_end)     bss, cleared by the loader
   [__noinit_start, slot end)   the reset surviving tail: nothing here. While modit.plg is loaded the recovery console answers from it
   SLOT_BYTES and NOINIT_BYTES are EXT_SLOT_BYTES and EXT_NOINIT_BYTES of ext_abi.h (pack_ext.py checks them). */
SLOT_BYTES = 48K;
NOINIT_BYTES = 2K;
SECTIONS
{
    . = MODULE_BASE;
    __ext_base = .;
    .text : { KEEP(*(.text.plug_entry)) *(.text*) *(.rodata*) . = ALIGN(4);
              __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .; . = ALIGN(8); }
    __rw_start = .;
    .data : { *(.data*) . = ALIGN(8); }
    __image_end = .;
    /* the pattern buffer of the IT import (__NOINIT(EXTERNAL_RAM), SDRAM in the image) is plain bss here: it need not survive a reset */
    .bss (NOLOAD) : { *(.bss*) *(COMMON) *(.noinit.$EXTERNAL_RAM*) . = ALIGN(8); }
    __bss_end = .;
    .noinit (MODULE_BASE + SLOT_BYTES - NOINIT_BYTES) (NOLOAD) : { __noinit_start = .; *(.noinit*) . = ALIGN(8); __noinit_end = .; }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT(__bss_end <= __noinit_start, "module text, data and bss overrun the slot's load part")
ASSERT(__noinit_end <= MODULE_BASE + SLOT_BYTES, "module records overrun the slot")
