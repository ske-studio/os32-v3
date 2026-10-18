/* VA0 ATAPI/ISO9660 body, shared by the product and T5b-5 size/relocation checks. */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(cd_module_init)
SECTIONS
{
    . = 0;
    .text : { *(.text .text.*) }
    .rodata ALIGN(4) : { *(.rodata .rodata.*) }
    .data ALIGN(4) : {
        KEEP(*(.data.cd_import_slots))
        *(.data .data.*)
    }
    .bss ALIGN(4) (NOLOAD) : { *(.bss .bss.*) *(COMMON) __module_end = ALIGN(4096); }
    .os32_generations 0 (INFO) : { KEEP(*(.os32_generations)) }
    .os32_generation_ref 0 (INFO) : { KEEP(*(.os32_generation_ref)) }
    /DISCARD/ : { *(.comment) *(.note.GNU-stack) *(.eh_frame) }
}
INCLUDE sdk/link/generations.ld
