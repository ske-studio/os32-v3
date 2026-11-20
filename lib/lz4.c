/* ======================================================================== */
/*  LZ4.C -- LZ4 ブロック形式 Safe デコーダ                                  */
/*                                                                          */
/*  LZ4ブロック仕様 (lz4_Block_format.md) に準拠。                           */
/*  公式 LZ4 / Python lz4.block で圧縮されたデータをそのまま展開可能。       */
/*                                                                          */
/*  C89 (GNU89), -ffreestanding 対応。ヒープ/BSS不使用。                     */
/*                                                                          */
/*  リンク先: program と PPC kernel の専用 object。                        */
/*  両者の最適化設定は各 build 規則を参照。boot は boot/lz4_mini.c。       */
/*  展開ループは HDD ローダと共有する。                               */
/*                                                                          */
/*  注意: -O0 でコンパイルすること。                                       */
/*  GCC -O2 のループ最適化がマッチコピーのバックリファレンス処理を           */
/*  不正に変換し、NULLガードページへのアクセスを引き起こす問題がある。       */
/* ======================================================================== */

#include "lz4.h"

/* LZ4定数 */
#define LZ4_TOKEN_BITS 4
#define LZ4_TOKEN_MASK 0x0F
#define LZ4_LEN_EXT LZ4_TOKEN_MASK
#define LZ4_OFFSET_BYTES 2

#define LZ4_MINMATCH  4   /* マッチの最小長 */

/*
 * lz4_decode: LZ4ブロックの Safe 展開
 *
 * LZ4ブロック形式:
 *   [Token(1B)] [拡張リテラル長]* [リテラルバイト列] [Offset(2B LE)] [拡張マッチ長]*
 *
 * Token:
 *   上位4bit = リテラル長 (0-14: そのまま, 15: 追加バイトで拡張)
 *   下位4bit = マッチ長 (0-14: +4, 15: +4 + 追加バイトで拡張)
 *
 * 最終シーケンス: リテラルのみ (Offset/マッチなし)
 */
int lz4_decode(const u8 *src, int compressed_size,
               u8 *dst, int dst_capacity)
{
    const u8 *ip;       /* 入力ポインタ */
    const u8 *ip_end;   /* 入力終端 */
    u8 *op;             /* 出力ポインタ */
    u8 *op_end;         /* 出力終端 */

    if (!src || !dst || compressed_size < 0 || dst_capacity < 0)
        return -1;

    ip     = src;
    ip_end = src + compressed_size;
    op     = dst;
    op_end = dst + dst_capacity;

#include "lz4_decode_core.inc"
}
