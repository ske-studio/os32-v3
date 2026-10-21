/* KMOD body VA zero; --emit-relocs is required by tools/mkmod.py.
 * Separate module link; never part of the resident kernel link. */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(hwdev_init)
SECTIONS
{
    . = 0;
    .text : { *(.text .text.*) }
    .rodata ALIGN(4) : { *(.rodata .rodata.*) }
    .data ALIGN(4) : {
        KEEP(*(.data.hwdev_import_slots))
        *(.data .data.*)
    }
    .bss ALIGN(4) (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
    .os32_generations 0 (INFO) : { KEEP(*(.os32_generations)) }
    .os32_generation_ref 0 (INFO) : { KEEP(*(.os32_generation_ref)) }
    /DISCARD/ : { *(.comment) *(.note.GNU-stack) *(.eh_frame) }
}
INCLUDE sdk/link/generations.ld
