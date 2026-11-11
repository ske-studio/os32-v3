/* HAL v2 packed pixel formats. Pixel bytes are little endian on every CPU. */
#include "gfx2_fmt.h"
#include "os32_kapi_shared.h" /* OS32_ERR_* for both kernel and userland. */

int gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *o)
{
    static const struct gfx2_fmt_info formats[] = {
        {8, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
        {16, 2, 0, 0, 11, 5, 5, 6, 0, 5, 0, 0},
        {32, 4, 0, 0, 16, 8, 8, 8, 0, 8, 0, 0},
        {32, 4, 1, 0, 16, 8, 8, 8, 0, 8, 24, 8},
        {16, 2, 0, 0, 10, 5, 5, 5, 0, 5, 0, 0},
        {16, 2, 1, 0, 10, 5, 5, 5, 0, 5, 15, 1},
        {8, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 8},
        {8, 1, 0, 0, 0, 8, 0, 8, 0, 8, 0, 0},
        {16, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {16, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    };
    if (fmt < GFX2_FMT_INDEX8 || fmt > GFX2_FMT_UYVY || !o)
        return OS32_ERR_INVAL;
    *o = formats[fmt - GFX2_FMT_INDEX8];
    return 0;
}

u32 gfx2_px_read(const u8 *p, u32 fmt)
{
    struct gfx2_fmt_info info;
    u32 v = 0, i;
    if (gfx2_fmt_info(fmt, &info)) return 0;
    for (i = 0; i < info.cpp; i++) v |= (u32)p[i] << (i * 8);
    return v;
}

void gfx2_px_write(u8 *p, u32 fmt, u32 v)
{
    struct gfx2_fmt_info info;
    u32 i;
    if (gfx2_fmt_info(fmt, &info)) return;
    for (i = 0; i < info.cpp; i++) p[i] = (u8)(v >> (i * 8));
}

static u32 expand(u32 v, u32 bits)
{
    if (bits == 5) return (v << 3) | (v >> 2);
    if (bits == 6) return (v << 2) | (v >> 4);
    if (bits == 1) return v ? 255 : 0;
    return v;
}

static u32 channel(u32 v, u32 shift, u32 bits)
{
    return bits ? expand((v >> shift) & ((1u << bits) - 1), bits) : 0;
}

u32 gfx2_to_argb(u32 fmt, u32 v, const u32 *pal256)
{
    struct gfx2_fmt_info f;
    u32 a, r, g, b;
    if (gfx2_fmt_info(fmt, &f)) return 0;
    if (fmt == GFX2_FMT_INDEX8) return 0xff000000u | (pal256[v & 255] & 0xffffffu);
    if (fmt == GFX2_FMT_YUY2 || fmt == GFX2_FMT_UYVY) return 0;
    a = f.has_alpha ? channel(v, f.as, f.ab) : 255;
    r = channel(v, f.rs, f.rb); g = channel(v, f.gs, f.gb); b = channel(v, f.bs, f.bb);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

int gfx2_from_argb(u32 fmt, u32 argb, u32 *v)
{
    struct gfx2_fmt_info f;
    u32 r, g, b, a;
    if (gfx2_fmt_info(fmt, &f) || !v) return OS32_ERR_INVAL;
    if (fmt == GFX2_FMT_INDEX8 || fmt == GFX2_FMT_YUY2 || fmt == GFX2_FMT_UYVY)
        return OS32_ERR_NOSYS;
    r = (argb >> 16) & 255; g = (argb >> 8) & 255; b = argb & 255; a = argb >> 24;
    if (fmt == GFX2_FMT_L8) { *v = r; return 0; }
    *v = (f.rb ? (r >> (8 - f.rb)) << f.rs : 0) |
         (f.gb ? (g >> (8 - f.gb)) << f.gs : 0) |
         (f.bb ? (b >> (8 - f.bb)) << f.bs : 0) |
         (f.ab ? (a >> (8 - f.ab)) << f.as : 0);
    if (fmt == GFX2_FMT_XRGB8888) *v |= 0xff000000u;
    if (fmt == GFX2_FMT_XRGB1555) *v |= 0x8000u;
    return 0;
}

static u32 clamp8(int x)
{
    return x < 0 ? 0 : x > 255 ? 255 : (u32)x;
}

u32 gfx2_yuv_argb(u8 y, u8 u, u8 v)
{
    int c = (int)y - 16, d = (int)u - 128, e = (int)v - 128;
    u32 r = clamp8((298 * c + 409 * e + 128) >> 8);
    u32 g = clamp8((298 * c - 100 * d - 208 * e + 128) >> 8);
    u32 b = clamp8((298 * c + 516 * d + 128) >> 8);
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

u32 gfx2_div255(u32 t)
{
    t += 128;
    return (t + (t >> 8)) >> 8;
}
