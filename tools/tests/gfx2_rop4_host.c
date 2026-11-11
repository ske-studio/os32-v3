#include "gfx/v2/gfx2_sw.h"
static u8 src[64], dst[64], mask[8];
u8 gfx2_sw_rowbuf[2][GFX2_SW_ROW_BYTES];
int gfx2_fmt_info(u32 f, struct gfx2_fmt_info *o) { (void)f; o->cpp = 1; return 0; }
u32 gfx2_px_read(const u8 *p, u32 f) { (void)f; return *p; }
void gfx2_px_write(u8 *p, u32 f, u32 v) { (void)f; *p = (u8)v; }
int gfxsync_read_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, u8 *p, u32 pitch)
{ u32 i; (void)h; (void)pitch; for (i = 0; i < w; i++) p[i] = ((u8 *)c)[y * 8 + x + i]; return 0; }
int gfxsync_write_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, const u8 *p, u32 pitch)
{ u32 i; (void)h; (void)pitch; for (i = 0; i < w; i++) ((u8 *)c)[y * 8 + x + i] = p[i]; return 0; }
static int probe(void)
{
    GFX2_Op op = {0}; struct gfx2_exec ex = {0}; struct gfx2_rect t;
    struct gfx2_pattern pat = {0}; u32 i;
    op.rop = 0x00cc; op.dw = 8; op.dh = 1; ex.op = &op;
    ex.sv.base = src; ex.dv.base = dst; ex.sv.pitch = ex.dv.pitch = 8;
    ex.sv.fmt = ex.dv.fmt = GFX2_FMT_INDEX8; ex.cpu = mask; ex.cpu_pitch = 1;
    ex.clip = (struct gfx2_rect){0, 0, 8, 1}; mask[0] = 0xf0;
    for (i = 0; i < 8; i++) { src[i] = (u8)(i + 1); dst[i] = 0xff; }
    if (gfx2_sw_rop4(&ex, &t)) return 1;
    for (i = 0; i < 8; i++) if (dst[i] != (i < 4 ? src[i] : 0)) return 2;
    if (t.x != 0 || t.y != 0 || t.w != 8 || t.h != 1) return 3;
    ex.clip = (struct gfx2_rect){2, 0, 4, 1}; ex.sv.sync = (GfxSyncCtx *)src; ex.dv.sync = (GfxSyncCtx *)dst;
    if (gfx2_sw_rop4(&ex, &t) || dst[2] != 3 || dst[3] != 4 || dst[4] != 0 || dst[5] != 0) return 4;
    op.flags = GFX2_F_PAT; op.rop = 0xf0f0; pat.kind = 1; pat.mono[0] = 0x20; pat.fg.v = 0x55; pat.bg.x = GFX2_CX_TRANSPARENT; ex.pat = &pat;
    if (gfx2_sw_rop4(&ex, &t) || dst[2] != 0x55 || t.x != 2 || t.w != 1) return 5;
    return 0;
}
void _start(void) { int rc = probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); for (;;) {} }
