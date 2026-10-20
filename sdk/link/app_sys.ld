#include "memmap_layout.h"
/* GENERATED GENERATION REF BEGIN */
__os32_abi_1_mem_4_fmt_4 = 0;
/* GENERATED GENERATION REF END */
__os32_shell_load_addr = LAYOUT_MEM_SHELL_LOAD_ADDR;
__os32_shell_sbrk_limit = LAYOUT_MEM_SHELL_HEAP_BASE; /* private CRT diagnostic */
ENTRY(_start)
SECTIONS
{
    /* シェル常駐帯域 (memmap.h: MEM_SHELL_LOAD_ADDR、カーネルと同じ生成ヘッダ) */
    . = LAYOUT_MEM_SHELL_LOAD_ADDR;

    .text : {
        /* crt0.asm の _start を必ず先頭に配置 */
        *(.text.startup)
        *(.text*)
    }

    .data : {
        *(.data)
        *(.data.*)
        *(.rodata*)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }
    _end = .;
    ASSERT(ALIGN(_end, LAYOUT_MEM_SHELL_HEAP_ALIGN) + LAYOUT_MEM_SHELL_IMAGE_HEADROOM
           <= LAYOUT_MEM_SHELL_HEAP_BASE, "resident image leaves less than 64KiB sbrk")

    /* KAPI データ欄の配置の刻印 (票 TASK_KAPI_DATA_FIELDS、ヘッダ v3)。
     * crt0 / os32api が置く 1 語を mkos32x.py が OS32X ヘッダの kapi_data_off
     * へ写す。INFO = 非ロード: objcopy -O binary の平らなバイナリに入らない。
     * --gc-sections で落ちないよう KEEP する。 */
    .os32_kapi_layout 0 (INFO) : {
        KEEP(*(.os32_kapi_layout))
    }

    .os32_generations 0 (INFO) : { KEEP(*(.os32_generations)) }
    .os32_generation_ref 0 (INFO) : { KEEP(*(.os32_generation_ref)) }

    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.eh_frame)
        *(.comment)
    }
}
