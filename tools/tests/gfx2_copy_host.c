#include "gfx/v2/gfx2_sw.h"
u8 gfx2_sw_rowbuf[2][GFX2_SW_ROW_BYTES];
static u8 pixels[32];
static struct gfx2_surf surf;
static int read_error, write_error;
static GFX2_Op op;
static struct gfx2_exec ex;
int gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *out)
{ (void)fmt; out->cpp = 1; return 0; }
int gfxsync_read_rows(GfxSyncCtx *ctx, u32 x, u32 y, u32 w, u32 h, u8 *dst, u32 pitch)
{ u32 i; (void)ctx; (void)h; (void)pitch; if (read_error) return read_error; for (i = 0; i < w; i++) dst[i] = pixels[y * 8 + x + i]; return 0; }
int gfxsync_write_rows(GfxSyncCtx *ctx, u32 x, u32 y, u32 w, u32 h, const u8 *src, u32 pitch)
{ u32 i; (void)ctx; (void)h; (void)pitch; if (write_error) return write_error; for (i = 0; i < w; i++) pixels[y * 8 + x + i] = src[i]; return 0; }
static int probe(void)
{
    struct gfx2_rect touched;
    u32 i;
    for (i = 0; i < 32; i++) pixels[i] = (u8)i;
    ex.op = &op; ex.src = ex.dst = &surf;
    ex.sv.base = ex.dv.base = pixels; ex.sv.pitch = ex.dv.pitch = 8;
    op.dx = 2; ex.clip.x = 2; ex.clip.w = 6; ex.clip.h = 1;
    if (gfx2_sw_copy(&ex, &touched) || touched.x != 2 || touched.w != 6) return 1;
    for (i = 2; i < 8; i++) if (pixels[i] != i - 2) return 2;
    for (i = 0; i < 32; i++) pixels[i] = (u8)i;
    op.dx = 0; op.dy = 1; ex.clip.x = 0; ex.clip.y = 1; ex.clip.w = 8; ex.clip.h = 3;
    ex.sv.sync = ex.dv.sync = (GfxSyncCtx *)&surf;
    if (gfx2_sw_copy(&ex, &touched)) return 3;
    for (i = 8; i < 32; i++) if (pixels[i] != i - 8) return 4;
    for (i = 0; i < 32; i++) pixels[i] = (u8)i;
    ex.sv.sync = ex.dv.sync = 0;
    op.dx = 0; op.dy = 0; op.sx = 1; op.sy = 1;
    ex.clip.x = 2; ex.clip.y = 1; ex.clip.w = 3; ex.clip.h = 1;
    if (gfx2_sw_copy(&ex, &touched)) return 5;
    for (i = 0; i < 3; i++) if (pixels[10 + i] != 19 + i) return 6;
    if (pixels[9] != 9 || pixels[13] != 13 || touched.y != 1 || touched.h != 1) return 7;
    ex.sv.sync = (GfxSyncCtx *)&surf; read_error = OS32_ERR_IO;
    if (gfx2_sw_copy(&ex, &touched) != OS32_ERR_IO) return 8;
    read_error = 0; ex.dv.sync = (GfxSyncCtx *)&surf; write_error = OS32_ERR_IO;
    if (gfx2_sw_copy(&ex, &touched) != OS32_ERR_IO) return 9;
    return 0;
}
void _start(void) { int rc = probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); for (;;) {} }
