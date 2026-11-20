/* KMOD body VA zero; --emit-relocs is required by tools/mkmod.py.
 * L1 mkmod integration and PPC FEP/KCG make rules must pass this flag at
 * final link. A relocation-free entry legitimately has no RELA records.
 * Separate module link; never part of the resident kernel link. */
OUTPUT_FORMAT(elf32-powerpc)
OUTPUT_ARCH(powerpc)
/* Select the component entry with -e at final link. */
SECTIONS
{
    . = 0;
    .text : { *(.text .text.*) }
    .rodata ALIGN(4) : { *(.rodata .rodata.*) }
    .data ALIGN(4) : {
        KEEP(*(.data.*_import_slots))
        *(.data .data.*)
    }
    .bss ALIGN(4) (NOLOAD) : { *(.bss .bss.*) *(COMMON) __module_end = ALIGN(4096); }
    .os32_generations 0 (INFO) : { KEEP(*(.os32_generations)) }
    .os32_generation_ref 0 (INFO) : { KEEP(*(.os32_generation_ref)) }
    /DISCARD/ : { *(.comment) *(.note.GNU-stack) *(.eh_frame) }
}
INCLUDE sdk/link/generations.ld
