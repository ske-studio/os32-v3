/* ============================================================
 * loader.ld — HDD新ローダー用リンカスクリプト
 *
 * 0x8000 に配置。ASMエントリが先頭に来るようにする。
 * BSS は NOLOAD (バイナリに含まれない)。
 * ASM側で __bss_start 〜 __bss_end をゼロクリアする。
 * ============================================================ */

ENTRY(loader_entry)

SECTIONS
{
    . = 0x8000;

    .text : {
        boot/loader_hdd.o(.text)
        *(.text)
        *(.text.*)
    }

    .data : {
        *(.data)
        *(.data.*)
        *(.rodata)
        *(.rodata.*)
    }

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.note*)
        *(.eh_frame)
        *(.comment)
    }
}
