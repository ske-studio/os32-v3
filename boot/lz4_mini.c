/* ======================================================================== */
/*  LZ4_MINI.C — ブートローダー用 LZ4 デコーダ                              */
/*  lib/lz4.c と同一ロジック。カーネルヘッダに依存しない自己完結版。          */
/*  ヒープ/BSS不使用。スタック消費 ~16B。                                    */
/* ======================================================================== */

#include "boot_defs.h"

#define LZ4_TOKEN_MASK LZ4_LENGTH_MASK
#define LZ4_LEN_EXT LZ4_LENGTH_MASK
#define LZ4_ERR_OUTPUT BOOT_LZ4_ERR_OUTPUT
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

#include "../lib/lz4_decode_core.inc"
}
