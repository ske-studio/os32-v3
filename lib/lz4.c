/* ======================================================================== */
/*  LZ4.C -- LZ4 ブロック形式 Safe デコーダ                                  */
/*                                                                          */
/*  LZ4ブロック仕様 (lz4_Block_format.md) に準拠。                           */
/*  公式 LZ4 / Python lz4.block で圧縮されたデータをそのまま展開可能。       */
/*                                                                          */
/*  C89 (GNU89), -ffreestanding 対応。ヒープ/BSS不使用。                     */
/*                                                                          */
/*  リンク先: プログラム用 (lz4_prog.o → lz4.bin 等)。                       */
/*  カーネル本体の lz4_decode は Rust 実装 (lib/os32_lz4) が提供し、          */
/*  ブートローダーは boot/lz4_mini.c を使う。境界チェックの修正は             */
/*  3 実装に同時に適用すること。                                             */
/*                                                                          */
/*  注意: -O0 でコンパイルすること (programs.mk lz4_prog.o で設定済み)。      */
/*  GCC -O2 のループ最適化がマッチコピーのバックリファレンス処理を           */
/*  不正に変換し、NULLガードページへのアクセスを引き起こす問題がある。       */
/* ======================================================================== */

#include "lz4.h"

/* LZ4定数 */
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

    /* メインデコードループ */
    for (;;) {
        int token;
        int lit_len;
        int match_len;
        int offset;
        const u8 *match_src;
        int i;

        /* トークン読み込み */
        if (ip >= ip_end)
            break;
        token = *ip++;

        /* --- リテラル --- */
        lit_len = (token >> 4) & 0x0F;
        if (lit_len == 15) {
            /* 拡張リテラル長 */
            int s;
            do {
                if (ip >= ip_end) return -1;
                s = *ip++;
                lit_len += s;
                /* 壊れた入力での長さ累積 (int オーバーフロー) を止める */
                if (lit_len > dst_capacity) return -1;
            } while (s == 255);
        }

        /* リテラルバイト列をコピー。
         * ip + lit_len 形式はポインタラップですり抜けるので差分比較 */
        if (lit_len > (int)(ip_end - ip)) return -1;
        if (lit_len > (int)(op_end - op)) return -2;
        for (i = 0; i < lit_len; i++) {
            *op++ = *ip++;
        }

        /* 最終シーケンス判定: 入力を全て消費したらここで終了 */
        if (ip >= ip_end)
            break;

        /* --- Offset (2バイト LE) --- */
        if (ip + 2 > ip_end) return -1;
        offset = (int)ip[0] | ((int)ip[1] << 8);
        ip += 2;

        if (offset == 0) return -1;        /* offset 0 は不正 */
        if (op - dst < offset) return -1;   /* 出力バッファ先頭を超える参照 */

        /* --- マッチ --- */
        match_len = (token & 0x0F) + LZ4_MINMATCH;
        if ((token & 0x0F) == 15) {
            /* 拡張マッチ長 */
            int s;
            do {
                if (ip >= ip_end) return -1;
                s = *ip++;
                match_len += s;
                if (match_len > dst_capacity) return -1;
            } while (s == 255);
        }

        /* マッチコピー (オーバーラップ対応: 1バイトずつコピー) */
        if (match_len > (int)(op_end - op)) return -2;
        match_src = op - offset;
        for (i = 0; i < match_len; i++) {
            *op++ = match_src[i];
        }
    }

    return (int)(op - dst);
}
