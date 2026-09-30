/* ======================================================================== */
/*  LZ4_MINI.C — ブートローダー用 LZ4 デコーダ                              */
/*  lib/lz4.c と同一ロジック。カーネルヘッダに依存しない自己完結版。          */
/*  ヒープ/BSS不使用。スタック消費 ~16B。                                    */
/* ======================================================================== */

#include "boot_defs.h"

#define LZ4_MINMATCH  4

int boot_lz4_decode(const u8 *src, int compressed_size,
                    u8 *dst, int dst_capacity)
{
    const u8 *ip     = src;
    const u8 *ip_end = src + compressed_size;
    u8 *op           = dst;
    u8 *op_end       = dst + dst_capacity;

    if (!src || !dst || compressed_size < 0 || dst_capacity < 0)
        return -1;

    for (;;) {
        int token, lit_len, match_len, offset, i;
        const u8 *match_src;

        if (ip >= ip_end) break;
        token = *ip++;

        /* リテラル */
        lit_len = (token >> 4) & 0x0F;
        if (lit_len == 15) {
            int s;
            do {
                if (ip >= ip_end) return -1;
                s = *ip++;
                lit_len += s;
                /* 壊れた入力での長さ累積 (int オーバーフロー) を止める。
                 * 出力容量を超えた時点で正当なストリームではない */
                if (lit_len > dst_capacity) return -1;
            } while (s == 255);
        }
        /* ip + lit_len 形式はポインタが上位アドレスでラップすると
         * すり抜けるので、差分 (ip_end - ip) と比較する */
        if (lit_len > (int)(ip_end - ip)) return -1;
        if (lit_len > (int)(op_end - op)) return -2;
        for (i = 0; i < lit_len; i++) *op++ = *ip++;

        if (ip >= ip_end) break;

        /* オフセット (2B LE) */
        if (ip + 2 > ip_end) return -1;
        offset = (int)ip[0] | ((int)ip[1] << 8);
        ip += 2;
        if (offset == 0) return -1;
        if (op - dst < offset) return -1;

        /* マッチ */
        match_len = (token & 0x0F) + LZ4_MINMATCH;
        if ((token & 0x0F) == 15) {
            int s;
            do {
                if (ip >= ip_end) return -1;
                s = *ip++;
                match_len += s;
                if (match_len > dst_capacity) return -1;
            } while (s == 255);
        }
        if (match_len > (int)(op_end - op)) return -2;
        match_src = op - offset;
        for (i = 0; i < match_len; i++) *op++ = match_src[i];
    }

    return (int)(op - dst);
}
