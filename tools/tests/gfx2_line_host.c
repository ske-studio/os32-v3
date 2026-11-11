#include "gfx2_sw.h"
static u8 pixels[64];
u8 gfx2_sw_rowbuf[2][GFX2_SW_ROW_BYTES];
static int fail_read, fail_write;
int gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *i) { (void)fmt; i->cpp = 1; return 0; }
u32 gfx2_px_read(const u8 *p, u32 fmt) { (void)fmt; return *p; }
void gfx2_px_write(u8 *p, u32 fmt, u32 v) { (void)fmt; *p = (u8)v; }
int gfxsync_read_rows(GfxSyncCtx *s, u32 x, u32 y, u32 w, u32 h, u8 *p, u32 pitch) {
    (void)s; (void)w; (void)h; (void)pitch; if (fail_read) return -7; *p = pixels[y * 8 + x]; return 0;
}
int gfxsync_write_rows(GfxSyncCtx *s, u32 x, u32 y, u32 w, u32 h, const u8 *p, u32 pitch) {
    (void)s; (void)w; (void)h; (void)pitch; if (fail_write) return -8; pixels[y * 8 + x] = *p; return 0;
}
static int run(void) {
    GFX2_Op op = { .dx=0, .dy=0, .sx=2, .sy=1, .rop=0xf0, .fg={0x5a,0} };
    struct gfx2_exec ex = { .op=&op, .dv={ .base=pixels, .pitch=8, .w=8, .h=8, .fmt=1 }, .clip={0,0,8,8} };
    struct gfx2_rect t;
    if (gfx2_sw_line(&ex, &t) || pixels[0]!=0x5a || pixels[1]!=0x5a || pixels[9] || t.x || t.y || t.w!=2 || t.h!=1) return 1;
    op.dx=4; op.dy=4; op.sx=1; op.sy=1; op.rop=0x5a; op.fg.v=0xff;
    ex.clip.x=2; ex.clip.y=2; ex.clip.w=2; ex.clip.h=2;
    ex.dv.sync=(GfxSyncCtx *)1;
    if (gfx2_sw_line(&ex, &t) || pixels[3*8+3]!=0xff || pixels[2*8+2]!=0xff || pixels[4*8+4] || t.x!=2 || t.y!=2 || t.w!=2 || t.h!=2) return 2;
    fail_read=1; if (gfx2_sw_line(&ex,&t)!=-7 || t.w) return 3;
    fail_read=0; fail_write=1; if (gfx2_sw_line(&ex,&t)!=-8 || t.w) return 4;
    ex.clip.w=0; if (gfx2_sw_line(&ex,&t) || t.w || t.h) return 5;
    return 0;
}
void _start(void) { int rc=run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
