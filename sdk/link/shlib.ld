/*
 * shlib.ld — 共有ライブラリ (OS32X_FLAG_SHLIB) のリンカスクリプト。票 C3。
 *
 * 配置 (正典: include/memmap.h の MEM_SHLIB_BASE、K3):
 *
 *   0x400000  .shlib_hdr   ちょうど 1 ページ。OS32ShlibHeader (32B) + entry[]。
 *                          カーネルはここを読んで版とページ数を知る。
 *   +0x1000   .text .rodata  read-only + USER。全プロセスの PD に同じ物理ページを貼る。
 *   (ページ境界) .data .bss   read/write。**アプリごとに別の物理ページ**にする。
 *
 * ヘッダの data_vaddr / data_pages / text_pages は下の絶対シンボルから埋まる
 * (libos32gui/src/shlib.rs の `.long __shlib_*`)。`tools/mkshlib.py` が ELF を
 * 読んで検算する。
 *
 * 位置依存 (再配置なし)。crt0 は使わない — 入口はジャンプ表であって _start ではない。
 */
ENTRY(__os32_shlib_header)
SECTIONS
{
    . = 0x400000;

    /* --- 先頭ページ: ヘッダ + ジャンプ表 (ちょうど 4KB に切り上げる) --- */
    .shlib_hdr : {
        KEEP(*(.shlib_hdr))
        . = ALIGN(4096);
    }

    /* --- 共有する read-only 部 --- */
    .text : {
        *(.text.startup)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    /* --- ここからアプリごとの物理ページ --- */
    . = ALIGN(4096);
    __shlib_data_start = .;

    .data : {
        *(.data)
        *(.data.*)
        /* PIC コード生成の名残 (GOT / 再配置つき read-only) も書き込み側へ。 */
        *(.data.rel.ro*)
        *(.got.plt)
        *(.got)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(4096);
    __shlib_data_end = .;
    _end = .;

    /* ヘッダに埋める値 (絶対シンボル)。 */
    __shlib_text_pages = ABSOLUTE((__shlib_data_start - 0x400000) / 4096);
    __shlib_data_pages = ABSOLUTE((__shlib_data_end - __shlib_data_start) / 4096);

    /* KAPI データ欄の配置の刻印 (票 TASK_KAPI_DATA_FIELDS、ヘッダ v3)。
     * shlib は crt0 を持たないので os32api (Rust) の刻印がここに入る。
     * mkshlib.py が OS32X ヘッダの kapi_data_off へ写す。INFO = 非ロード。 */
    .os32_kapi_layout 0 (INFO) : {
        KEEP(*(.os32_kapi_layout))
    }

    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.eh_frame)
        *(.comment)
    }
}
