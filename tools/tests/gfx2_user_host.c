#include "userland/lib/gfx/v2/os32gfx2u.h"

static u32 pal[256];
static int probe(void)
{
    u32 out, i;
    /* The public U1 header also exposes the generated KAPI declarations. */
    typedef int (*submit_fn)(u32, const GFX2_Op *, u32, u32, u32 *, u32 *);
    _Static_assert(__builtin_types_compatible_p(__typeof__(((KernelAPI *)0)->gfx2_submit), submit_fn), "submit export");
    _Static_assert(sizeof(((KernelAPI *)0)->gfx2_lock) == sizeof(void *), "lock export");
    for (i = 0; i < 256; i++) pal[i] = 0xffffffu;
    pal[255] = 0x00112233u;
    if (gfx2_color_pack(GFX2_FMT_INDEX8, 0xfe112233u, pal, &out) || out != 255) return 1;
    /* Alpha is ignored, and identical RGB entries keep the smaller index. */
    pal[7] = 0xaa112233u;
    if (gfx2_color_pack(GFX2_FMT_INDEX8, 0x00112233u, pal, &out) || out != 7) return 2;
    pal[7] = 0x00112231u;
    pal[255] = 0xff112235u;
    if (gfx2_color_pack(GFX2_FMT_INDEX8, 0x80112233u, pal, &out) || out != 7) return 3;
    /* Squared RGB distance: (3,0,0) beats (2,2,2), though its L1 sum is smaller. */
    for (i = 0; i < 256; i++) pal[i] = 0xffffffu;
    pal[0] = 0x020202u; pal[1] = 0x030000u;
    if (gfx2_color_pack(GFX2_FMT_INDEX8, 0, pal, &out) || out != 1) return 4;
    for (i = 0; i < 3; i++) {
        u32 shift = 8 * i;
        for (u32 j = 0; j < 256; j++) pal[j] = j << shift;
        for (u32 j = 0; j < 256; j++)
            if (gfx2_color_pack(GFX2_FMT_INDEX8, j << shift, pal, &out) || out != j) return 5;
    }
    for (i = 0; i < 256; i++) pal[i] = 0;
    if (gfx2_color_pack(GFX2_FMT_INDEX8, 0xffffffffu, pal, &out) || out != 0) return 6;
    out = 0x12345678u;
    if (gfx2_color_pack(GFX2_FMT_INDEX8, 0, 0, &out) != OS32_ERR_INVAL || out != 0x12345678u ||
        gfx2_color_pack(GFX2_FMT_INDEX8, 0, pal, 0) != OS32_ERR_INVAL) return 7;
    for (i = 0; i < 256; i++) {
        u32 c = i * 0x01010101u;
        if (gfx2_color_pack(GFX2_FMT_RGB565, c, 0, &out) ||
            out != ((i >> 3) << 11 | (i >> 2) << 5 | (i >> 3))) return 8;
        if (gfx2_color_pack(GFX2_FMT_ARGB8888, c, 0, &out) || out != c ||
            gfx2_color_pack(GFX2_FMT_XRGB8888, c, 0, &out) || out != (c | 0xff000000u)) return 9;
        if (gfx2_color_pack(GFX2_FMT_ARGB1555, c, 0, &out) ||
            out != ((i >> 7) << 15 | (i >> 3) << 10 | (i >> 3) << 5 | (i >> 3))) return 10;
        if (gfx2_color_pack(GFX2_FMT_XRGB1555, c, 0, &out) ||
            out != (0x8000u | (i >> 3) << 10 | (i >> 3) << 5 | (i >> 3))) return 11;
        if (gfx2_color_pack(GFX2_FMT_A8, c, 0, &out) || out != i ||
            gfx2_color_pack(GFX2_FMT_L8, c, 0, &out) || out != i) return 12;
    }
    const u32 bad[] = {0, 11, 0x40, 0x7f, ~0u};
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        out = 0x12345678u;
        if (gfx2_color_pack(bad[i], 0, pal, &out) != OS32_ERR_INVAL || out != 0x12345678u) return 13;
    }
    for (i = GFX2_FMT_YUY2; i <= GFX2_FMT_UYVY; i++) {
        out = 0x12345678u;
        if (gfx2_color_pack(i, 0, pal, &out) != OS32_ERR_NOSYS || out != 0x12345678u) return 14;
    }
    if (gfx2_color_pack(GFX2_FMT_RGB565, 0, 0, 0) != OS32_ERR_INVAL) return 15;
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
