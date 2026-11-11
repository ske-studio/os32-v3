#include "gfx/v2/gfx2_sw.h"
u8 gfx2_sw_rowbuf[2][GFX2_SW_ROW_BYTES];
int gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *o) { if (fmt != 1) return -1; o->cpp = 1; return 0; }
u32 gfx2_px_read(const u8 *p, u32 fmt) { (void)fmt; return *p; }
void gfx2_px_write(u8 *p, u32 fmt, u32 v) { (void)fmt; *p = v; }
static u8 sync_pixels[8];
static int fail_write;
int gfxsync_read_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, u8 *p, u32 pitch)
{ (void)c; (void)y; (void)h; (void)pitch; for (u32 i = 0; i < w; ++i) p[i] = sync_pixels[x+i]; return 0; }
int gfxsync_write_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, const u8 *p, u32 pitch)
{ (void)c; (void)y; (void)h; (void)pitch; if (fail_write) return OS32_ERR_IO; for (u32 i = 0; i < w; ++i) sync_pixels[x+i] = p[i]; return 0; }
static int test(void)
{
    u8 dst[8] = {15,15,15,15,15,15,15,15}, src[8] = {255,255,255,255,255,255,255,255};
    GFX2_Op op = { .op = GFX2_OP_COPY, .rop = 0x66, .dw = 8, .dh = 1 };
    struct gfx2_pattern pat = { .kind = 1, .mono = {0xaa}, .fg = {255,0}, .bg = {0,0} };
    struct gfx2_exec ex = { .op = &op, .dv = { .base = dst, .pitch = 8, .fmt = 1 },
        .sv = { .base = src, .pitch = 8, .fmt = 1 }, .clip = {0,0,8,1}, .pat = &pat };
    struct gfx2_rect t;
    if (gfx2_sw_rop(&ex, &t) || t.w != 8 || t.h != 1) return 1;
    for (int i = 0; i < 8; ++i) if (dst[i] != 0xf0) return 2;
    for (int i = 0; i < 8; ++i) dst[i] = 15;
    op.op = GFX2_OP_FILL; op.rop = 0x5a; op.flags = GFX2_F_PAT;
    if (gfx2_sw_rop(&ex, &t)) return 3;
    for (int i = 0; i < 8; ++i) if (dst[i] != (i % 2 ? 15 : 0xf0)) return 4;
    pat.mono[0] = 0; pat.bg.x = GFX2_CX_TRANSPARENT;
    if (gfx2_sw_rop(&ex, &t) || t.w || t.h) return 5;
    op.flags = 0; op.fg.v = 0xff; ex.dv.sync = (GfxSyncCtx *)1;
    fail_write = 1;
    if (gfx2_sw_rop(&ex, &t) != OS32_ERR_IO || t.h) return 6;
    fail_write = 0;
    if (gfx2_sw_rop(&ex, &t) || sync_pixels[0] != 0xff) return 7;
    ex.dv.sync = 0; ex.sv.base = dst; ex.src = ex.dst;
    for (int i = 0; i < 8; ++i) dst[i] = i;
    op.op = GFX2_OP_COPY; op.rop = 0xcc; op.dx = 2;
    ex.clip = (struct gfx2_rect){2,0,6,1};
    if (gfx2_sw_rop(&ex, &t)) return 8;
    for (int i = 2; i < 8; ++i) if (dst[i] != i - 2) return 9;
    return 0;
}
void _start(void) { int rc = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
