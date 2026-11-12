/* GENERATED GENERATION REF BEGIN */
__os32_abi_1_mem_4_fmt_5 = 0;
/* GENERATED GENERATION REF END */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)
SECTIONS
{
    . = 0x80100000;
    .text : {
        *(.text.startup)
        *(.text*)
        *(.rodata*)
        __text_used_end = .;
        . = ALIGN(4096);
        __text_end = .;
    }
    .data : {
        *(.data)
        *(.data.*)
        . = ALIGN(ALIGNOF(.bss));
    }
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }
    _end = .;
    .os32_kapi_layout 0 (INFO) : { KEEP(*(.os32_kapi_layout)) }
    .os32_generations 0 (INFO) : { KEEP(*(.os32_generations)) }
    .os32_generation_ref 0 (INFO) : { KEEP(*(.os32_generation_ref)) }
    /DISCARD/ : { *(.note.gnu.build-id) *(.eh_frame) *(.comment) }
}
