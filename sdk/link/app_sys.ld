ENTRY(_start)
SECTIONS
{
    /* シェル常駐帯域: 0x300000 (memmap.h: MEM_SHELL_LOAD_ADDR) */
    . = 0x300000;

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

    /* KAPI データ欄の配置の刻印 (票 TASK_KAPI_DATA_FIELDS、ヘッダ v3)。
     * crt0 / os32api が置く 1 語を mkos32x.py が OS32X ヘッダの kapi_data_off
     * へ写す。INFO = 非ロード: objcopy -O binary の平らなバイナリに入らない。
     * --gc-sections で落ちないよう KEEP する。 */
    .os32_kapi_layout 0 (INFO) : {
        KEEP(*(.os32_kapi_layout))
    }

    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.eh_frame)
        *(.comment)
    }
}
