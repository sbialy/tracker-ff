/* Plugin link (ABI 1): one image linked at address 0, entry first. build_plug.py turns the R_ARM_ABS32 relocations kept by
   --emit-relocs into the rebase table and refuses every other absolute relocation type. */
ENTRY(plug_entry)
SECTIONS
{
    . = 0;
    .text : {
        KEEP(*(.text.plug_entry))
        *(.text .text.*)
        . = ALIGN(4);
        __plug_desc = .;
        KEEP(*(.rodata.plug_desc))
        *(.rodata .rodata.*)
        *(.data.rel.ro .data.rel.ro.*)   /* constants holding addresses: read only once rebased */
        . = ALIGN(4);
    }
    . = ALIGN(8);
    __rw_start = .;
    .data : {
        *(.data .data.*)
        . = ALIGN(4);
    }
    __image_end = .;
    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
    }
    __bss_end = .;
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
