#include "gfx2_sw.h"

static u8 pixels[64];
static int fail_sync;
int gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *out)
{
    if (fmt != GFX2_FMT_INDEX8 && fmt != GFX2_FMT_RGB565) return OS32_ERR_INVAL;
    out->cpp = fmt == GFX2_FMT_INDEX8 ? 1 : 2;
    return 0;
}
void gfx2_px_write(u8 *p, u32 fmt, u32 v)
{
    p[0] = (u8)v;
    if (fmt == GFX2_FMT_RGB565) p[1] = (u8)(v >> 8);
}
int gfxsync_write_rows(GfxSyncCtx *ctx, u32 x, u32 y, u32 w, u32 h, const u8 *src, u32 pitch)
{
    (void)ctx;
    if (fail_sync) return OS32_ERR_IO;
    if (w != 1 || h != 1 || pitch != 2) return OS32_ERR_INVAL;
    pixels[y * 10 + x * 2] = src[0];
    pixels[y * 10 + x * 2 + 1] = src[1];
    return 0;
}
static int test(void)
{
    GFX2_Op op = {0};
    struct gfx2_exec ex = {0};
    struct gfx2_rect touched;
    op.fg.v = 7;
    ex.op = &op; ex.dv.base = pixels; ex.dv.pitch = 4; ex.dv.fmt = GFX2_FMT_INDEX8;
    ex.clip = (struct gfx2_rect){1, 1, 2, 2};
    if (gfx2_sw_fill(&ex, &touched)) return 1;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            if (pixels[y * 4 + x] != ((x >= 1 && x < 3 && y >= 1 && y < 3) ? 7 : 0)) return 2;
    if (touched.x != 1 || touched.y != 1 || touched.w != 2 || touched.h != 2) return 3;
    ex.clip.w = 0;
    if (gfx2_sw_fill(&ex, &touched) || touched.w || touched.h) return 4;
    ex.clip = (struct gfx2_rect){1, 1, 2, 1};
    ex.dv.sync = (GfxSyncCtx *)pixels; ex.dv.fmt = GFX2_FMT_RGB565; op.fg.v = 0x1234;
    if (gfx2_sw_fill(&ex, &touched) || pixels[12] != 0x34 || pixels[13] != 0x12 || pixels[14] != 0x34 || pixels[15] != 0x12) return 5;
    fail_sync = 1;
    if (gfx2_sw_fill(&ex, &touched) != OS32_ERR_IO || touched.w || touched.h) return 6;
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
