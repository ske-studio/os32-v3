#include "gfx/v2/gfx2_sw.h"
static u8 dst[16], src[8] = {1, 3, 9, 9, 3, 4, 9, 9};
static int sync_calls, fail;
int gfx2_fmt_info(u32 f, struct gfx2_fmt_info *o) { if (f != GFX2_FMT_INDEX8) return OS32_ERR_INVAL; o->cpp = 1; return 0; }
u32 gfx2_px_read(const u8 *p, u32 f) { (void)f; return *p; }
void gfx2_px_write(u8 *p, u32 f, u32 v) { (void)f; *p = (u8)v; }
int gfxsync_write_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, const u8 *p, u32 pitch)
{
    (void)c; (void)pitch;
    if (w != 1 || h != 1) return OS32_ERR_INVAL;
    sync_calls++;
    if (fail) return OS32_ERR_IO;
    dst[y * 4 + x] = *p; return 0;
}
static int probe(void)
{
    GFX2_Op op = {0}; struct gfx2_exec ex = {0}; struct gfx2_rect t;
    op.dx = 1; op.dy = 1; op.flags = GFX2_F_SRCKEY; op.skey.v = op.skey.x = 3;
    ex.op = &op; ex.cpu = src; ex.cpu_pitch = 4; ex.dv.base = dst; ex.dv.pitch = 4;
    ex.dv.fmt = GFX2_FMT_INDEX8; ex.clip = (struct gfx2_rect){1,1,2,2};
    if (gfx2_sw_hostdata(&ex, &t) || dst[5] != 1 || dst[6] || dst[9] || dst[10] != 4 || t.x != 1 || t.y != 1 || t.w != 2 || t.h != 2) return 1;
    op.flags = 0; ex.clip = (struct gfx2_rect){2,1,1,2};
    if (gfx2_sw_hostdata(&ex, &t) || dst[6] != 3 || dst[10] != 4 || t.x != 2 || t.w != 1 || t.h != 2) return 2;
    op.flags = GFX2_F_SRCKEY; op.skey.v = 1; op.skey.x = 4; ex.clip = (struct gfx2_rect){1,1,2,2};
    if (gfx2_sw_hostdata(&ex, &t) || t.w || t.h) return 3;
    op.skey.v = op.skey.x = 3; ex.dv.sync = (GfxSyncCtx *)1;
    if (gfx2_sw_hostdata(&ex, &t) || sync_calls != 2) return 4;
    fail = 1; if (gfx2_sw_hostdata(&ex, &t) != OS32_ERR_IO || t.w) return 5;
    return 0;
}
void _start(void) { int rc = probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); for (;;) {} }
