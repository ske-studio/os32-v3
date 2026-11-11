#include "gfx/v2/gfx2_sw.h"

static u8 pixels[48], out[32];
static int calls, fail_row;
static GfxSyncCtx *const sync_ctx = (GfxSyncCtx *)1;
int gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *info)
{
    if (fmt != GFX2_FMT_RGB565) return OS32_ERR_INVAL;
    info->cpp = 2;
    return 0;
}
int gfxsync_read_rows(GfxSyncCtx *ctx, u32 x, u32 y, u32 w, u32 h, u8 *dst, u32 pitch)
{
    ++calls;
    if (ctx != sync_ctx || x != 1 || y != (u32)calls || w != 2 || h != 1 || pitch != 7)
        return OS32_ERR_INVAL;
    if (calls == fail_row) return OS32_ERR_IO;
    for (u32 i = 0; i < w * 2; ++i) dst[i] = pixels[y * 12 + x * 2 + i];
    return 0;
}
static int test(void)
{
    GFX2_Op op = { .sx = 1, .sy = 1, .sw = 2, .sh = 2, .pitch = 7 };
    struct gfx2_exec ex = { .op = &op, .sv = { .base = pixels, .pitch = 12, .fmt = GFX2_FMT_RGB565 } };
    for (u32 i = 0; i < sizeof(pixels); ++i) pixels[i] = (u8)(i + 1);
    for (u32 i = 0; i < sizeof(out); ++i) out[i] = 0xa5;
    if (gfx2_sw_readback(&ex, out)) return 1;
    for (u32 y = 0; y < 2; ++y) {
        for (u32 x = 0; x < 4; ++x) if (out[y * 7 + x] != pixels[(y + 1) * 12 + 2 + x]) return 2;
        for (u32 x = 4; x < 7; ++x) if (out[y * 7 + x] != 0xa5) return 3;
    }
    ex.sv.sync = sync_ctx;
    for (u32 i = 0; i < sizeof(out); ++i) out[i] = 0;
    if (gfx2_sw_readback(&ex, out) || calls != 2) return 4;
    for (u32 y = 0; y < 2; ++y)
        for (u32 x = 0; x < 4; ++x)
            if (out[y * 7 + x] != pixels[(y + 1) * 12 + 2 + x]) return 8;
    calls = 0; fail_row = 2;
    if (gfx2_sw_readback(&ex, out) != OS32_ERR_IO || calls != 2) return 5;
    calls = 0; op.sw = 0;
    if (gfx2_sw_readback(&ex, out) || calls) return 6;
    op.sw = 2; ex.sv.fmt = 0;
    if (gfx2_sw_readback(&ex, out) != OS32_ERR_INVAL) return 7;
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
