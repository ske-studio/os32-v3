/* GENERATED GENERATION REF BEGIN */
__os32_abi_2_mem_4_fmt_5 = 0;
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
        *(.sdata2*)
        KEEP(*(.init))
        KEEP(*(.fini))
        __text_used_end = .;
        . = ALIGN(4096);
        __text_end = .;
    }
    .data : {
        *(.data)
        *(.data.*)
        . = ALIGN(8);
        *(.sdata .sdata.*)
        *(.got .got.* .got2 .fixup)
        KEEP(*(.preinit_array .preinit_array.*))
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        . = ALIGN(16);
        . = ALIGN(ALIGNOF(.bss));
    }
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss .sbss.* .sbss2 .sbss2.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    _end = .;
    .os32_kapi_layout 0 (INFO) : { KEEP(*(.os32_kapi_layout)) }
    .os32_generations 0 (INFO) : { KEEP(*(.os32_generations)) }
    .os32_generation_ref 0 (INFO) : { KEEP(*(.os32_generation_ref)) }
    /DISCARD/ : { *(.note.gnu.build-id) *(.eh_frame) *(.comment) }
}
