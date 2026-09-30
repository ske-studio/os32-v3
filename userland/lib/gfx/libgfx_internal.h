#ifndef __LIBGFX_INTERNAL_H
#define __LIBGFX_INTERNAL_H

#include "libos32gfx.h"

static inline void _memcpy_w(void *dst, const void *src, unsigned int words) {
    __asm__ volatile("rep movsw" : "+D"(dst), "+S"(src), "+c"(words) : : "memory");
}

static inline void _memcpy_d(void *dst, const void *src, unsigned int dwords) {
    __asm__ volatile("rep movsl" : "+D"(dst), "+S"(src), "+c"(dwords) : : "memory");
}

static inline void _memset_w(void *dst, unsigned int val16, unsigned int words) {
    __asm__ volatile("rep stosw" : "+D"(dst), "+c"(words) : "a"((unsigned short)val16) : "memory");
}

static inline void _memset_d(void *dst, unsigned int val32, unsigned int dwords) {
    __asm__ volatile("rep stosl" : "+D"(dst), "+c"(dwords) : "a"(val32) : "memory");
}

/* ------------------------------------------------------------------------ */
/*  パックド 8bpp (PEGC) 共通ヘルパ — 票 H2b                                  */
/*                                                                            */
/*  プレーン経路 (9801) は一切通らない。gfx_packed が 0 のときは従来どおり     */
/*  asm_* の高速経路へ落ちる (回帰ゼロ)。パックドでは 1 バイト 1 画素なので    */
/*  「先頭 = planes[0] + y*pitch + x」だけを見ればよい。                       */
/* ------------------------------------------------------------------------ */

extern void __cdecl asm_gfx_hline(u8 **planes, int base, int x, int x2, u8 color);

/* 画面 1 行のスパン [x, x2] を塗る。base は y * gfx_fb.pitch (両形式共通)。
 * 呼び出し側が x/x2/y をクリップ済みであることを前提にする (従来の
 * asm_gfx_hline と同じ契約)。 */
static inline void gfx_span_raw(int base, int x, int x2, u8 color)
{
    if (gfx_packed) {
        u8 *d = gfx_fb.planes[0] + base + x;
        int n = x2 - x + 1;
        if (n <= 0) return;
        while (n--) *d++ = color;
        return;
    }
    asm_gfx_hline(gfx_fb.planes, base, x, x2, color);
}

/* 画面 1 画素の書き/読み (クリップ込み)。パックド専用。 */
static inline void gfx_p_put(int x, int y, u8 color)
{
    if (x < 0 || x >= gfx_fb.width || y < 0 || y >= gfx_fb.height) return;
    gfx_fb.planes[0][y * gfx_fb.pitch + x] = color;
}

static inline u8 gfx_p_get(int x, int y)
{
    if (x < 0 || x >= gfx_fb.width || y < 0 || y >= gfx_fb.height) return 0;
    return gfx_fb.planes[0][y * gfx_fb.pitch + x];
}

#define SURF_POOL_MAX  16
#define SURF_DATA_SIZE 2048

typedef struct {
    GFX_Surface surf;
    u8 data[4][SURF_DATA_SIZE];
    u8 used;
} SurfSlot;

#define SPR_SIZE_16  48
#define SPR_SIZE_32  160
#define SPR_SIZE_64  576
#define SPR_SIZE_128 2176

#define SPR_MAX_16   64
#define SPR_MAX_32   32
#define SPR_MAX_64   16
#define SPR_MAX_128  4

#define SPR_POOL_MAX (SPR_MAX_16 + SPR_MAX_32 + SPR_MAX_64 + SPR_MAX_128)

#define SPR_TYPE_NONE 0
#define SPR_TYPE_16   1
#define SPR_TYPE_32   2
#define SPR_TYPE_64   3
#define SPR_TYPE_128  4

typedef struct {
    GFX_Sprite spr;
    u8 used;
    u8 pool_type;
    int pool_idx;
    u8 *data_base;
} SprSlot;

typedef struct {
    u8 data_16[SPR_MAX_16][8][4][SPR_SIZE_16];
    u8 mask_16[SPR_MAX_16][8][SPR_SIZE_16];
    u8 bg_16[SPR_MAX_16][4][SPR_SIZE_16];
    u8 used_16[SPR_MAX_16];

    u8 data_32[SPR_MAX_32][8][4][SPR_SIZE_32];
    u8 mask_32[SPR_MAX_32][8][SPR_SIZE_32];
    u8 bg_32[SPR_MAX_32][4][SPR_SIZE_32];
    u8 used_32[SPR_MAX_32];

    u8 data_64[SPR_MAX_64][8][4][SPR_SIZE_64];
    u8 mask_64[SPR_MAX_64][8][SPR_SIZE_64];
    u8 bg_64[SPR_MAX_64][4][SPR_SIZE_64];
    u8 used_64[SPR_MAX_64];

    u8 data_128[SPR_MAX_128][8][4][SPR_SIZE_128];
    u8 mask_128[SPR_MAX_128][8][SPR_SIZE_128];
    u8 bg_128[SPR_MAX_128][4][SPR_SIZE_128];
    u8 used_128[SPR_MAX_128];
} SprDataPool;

#endif
