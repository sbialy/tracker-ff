/* Plugin link (ABI 1): one image linked at address 0, entry first. build_plug.py turns the R_ARM_ABS32 relocations kept by
   --emit-relocs into the rebase table and refuses every other absolute relocation type. */
ENTRY(plug_entry)
SECTIONS
{
    . = 0;
    .text : {
        KEEP(*(.text.plug_entry))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    }
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
