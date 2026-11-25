/* extras.plg, the bundle of the release card modules: linked at the image's release region (MODULE_BASE, the image symbol s_extRel, REGION_BYTES
   = ext_abi.h EXT_REL_BYTES, both given by build_extras.sh) against the image's own symbols (ld --just-symbols), packed by
   synth-open/plug/pack_ext.py --region rel. Never in the console module's slot (s_ext), so both are loaded at the same time.
   The tenants sit back to back, each one starting at a 1 KB boundary of the region (ext_abi.h EXT_SUB_ALIGN, counted from MODULE_BASE: s_extRel
   itself is only 16 B aligned), in the order of ext_abi.h EXT_SUB_*:
     modit     the bundle entry and MOD import, IT import, IT export (modit_entry.cpp, modit_dispatch.cpp and the three file manager sources)
     theme     the custom theme JSON parser (core/source/ThemeManagerJson.cpp)
     selftest  the factory self test (core/source/mtTest.cpp)
     takefile  the Capture take file writer and reader (core/source/mtSequencer_captureFile.cpp)
     capture   the Capture take logic (core/source/mtSequencer_captureMod.cpp, ext_hooks.capture), its note feed tap stays in the image
   No sub-slot has a fixed size: the linker places each tenant from its real size and pack_ext.py writes the table it reads from the
   __sub_<name>_start / _end symbols into the file (ext_rel_table_t). Every tenant but the last carries its bss as zeros in the file (the next
   tenant follows it), the last one's bss is the file's bss (cleared by the loader). No reset surviving tail: __noinit_start is the region end
   and nothing is placed there. */
EXT_SUB_ALIGN = 1K;
SECTIONS
{
    . = MODULE_BASE;
    __ext_base = .;
    __sub_modit_start = .;
    .text : { KEEP(*(.text.plug_entry))
              *(EXCLUDE_FILE(*mtSequencer_captureMod.o *mtSequencer_captureFile.o *ThemeManagerJson.o *mtTest.o) .text*
                EXCLUDE_FILE(*mtSequencer_captureMod.o *mtSequencer_captureFile.o *ThemeManagerJson.o *mtTest.o) .rodata*)
              . = ALIGN(4); __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .; . = ALIGN(8); }
    __rw_start = .;
    .data : { *(EXCLUDE_FILE(*mtSequencer_captureMod.o *mtSequencer_captureFile.o *ThemeManagerJson.o *mtTest.o) .data*) . = ALIGN(8); }
    /* the pattern buffer of the IT import (__NOINIT(EXTERNAL_RAM), SDRAM in the image) is plain bss here: it need not survive a reset */
    .bss_modit (NOLOAD) : { *(EXCLUDE_FILE(*mtSequencer_captureMod.o *mtSequencer_captureFile.o *ThemeManagerJson.o *mtTest.o) .bss*
                              EXCLUDE_FILE(*mtSequencer_captureMod.o *mtSequencer_captureFile.o *ThemeManagerJson.o *mtTest.o) COMMON)
                            *(EXCLUDE_FILE(*mtSequencer_captureMod.o *mtSequencer_captureFile.o *ThemeManagerJson.o *mtTest.o) .noinit.$EXTERNAL_RAM*) . = ALIGN(8); }
    __sub_modit_end = .;
    .theme (MODULE_BASE + ALIGN(. - MODULE_BASE, EXT_SUB_ALIGN)) : { __sub_theme_start = .; *ThemeManagerJson.o(.text* .rodata* .data* .bss* COMMON .noinit.$EXTERNAL_RAM*) . = ALIGN(8); __sub_theme_end = .; }
    .selftest (MODULE_BASE + ALIGN(. - MODULE_BASE, EXT_SUB_ALIGN)) : { __sub_selftest_start = .; *mtTest.o(.text* .rodata* .data* .bss* COMMON .noinit.$EXTERNAL_RAM*) . = ALIGN(8); __sub_selftest_end = .; }
    .takefile (MODULE_BASE + ALIGN(. - MODULE_BASE, EXT_SUB_ALIGN)) : { __sub_takefile_start = .; *mtSequencer_captureFile.o(.text* .rodata* .data* .bss* COMMON .noinit.$EXTERNAL_RAM*) . = ALIGN(8); __sub_takefile_end = .; }
    .cap (MODULE_BASE + ALIGN(. - MODULE_BASE, EXT_SUB_ALIGN)) : { __sub_capture_start = .; *mtSequencer_captureMod.o(.text* .rodata* .data*) . = ALIGN(8); }
    __image_end = .;
    .bss (NOLOAD) : { *mtSequencer_captureMod.o(.bss* COMMON .noinit.$EXTERNAL_RAM*) . = ALIGN(8); }
    __bss_end = .;
    __sub_capture_end = .;
    .noinit (MODULE_BASE + REGION_BYTES) (NOLOAD) : { __noinit_start = .; *(.noinit*) . = ALIGN(8); __noinit_end = .; }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT(__sub_capture_end <= MODULE_BASE + REGION_BYTES, "extras.plg: the tenants need more than the release region (ext_abi.h EXT_REL_BYTES)")
ASSERT(__noinit_end == __noinit_start, "extras.plg has no reset surviving records")
