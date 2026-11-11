#include "gfx2_sw.h"
u8 gfx2_sw_rowbuf[2][16384];
static u32 source[8], dest[8];
static int fail_read, fail_write;
struct GfxSyncCtx { u32 *pixels; };
int gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *o) { if (fmt != GFX2_FMT_ARGB8888) return OS32_ERR_INVAL; o->cpp = 4; return 0; }
u32 gfx2_px_read(const u8 *p, u32 fmt) { (void)fmt; return *(const u32 *)p; }
void gfx2_px_write(u8 *p, u32 fmt, u32 v) { (void)fmt; *(u32 *)p = v; }
u32 gfx2_to_argb(u32 fmt, u32 v, const u32 *pal) { (void)fmt; (void)pal; return v; }
int gfx2_from_argb(u32 fmt, u32 v, u32 *out) { (void)fmt; *out = v; return 0; }
u32 gfx2_div255(u32 t) { t += 128; return (t + (t >> 8)) >> 8; }
int gfxsync_read_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, u8 *out, u32 pitch)
{ u32 i; (void)h; (void)pitch; if (fail_read) return OS32_ERR_IO; for (i = 0; i < w; i++) ((u32 *)out)[i] = c->pixels[y * 4 + x + i]; return 0; }
int gfxsync_write_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, const u8 *in, u32 pitch)
{ u32 i; (void)h; (void)pitch; if (fail_write) return OS32_ERR_IO; for (i = 0; i < w; i++) c->pixels[y * 4 + x + i] = ((const u32 *)in)[i]; return 0; }
static int probe(void)
{
    GFX2_Op op = {0}; struct gfx2_exec ex = {0}; struct gfx2_rect t;
    struct GfxSyncCtx s = {source}, d = {dest};
    op.dw = 4; op.dh = 1; op.sw = 2; op.sh = 1; op.fg.x = 255; op.flags = GFX2_F_ALPHA_PIXEL;
    ex.op = &op; ex.clip = (struct gfx2_rect){0, 0, 4, 1};
    ex.sv = (struct gfx2_view){.base=(u8 *)source, .pitch=16, .fmt=GFX2_FMT_ARGB8888};
    ex.dv = (struct gfx2_view){.base=(u8 *)dest, .pitch=16, .fmt=GFX2_FMT_ARGB8888};
    source[0] = 0x80404040u; source[1] = 0xff101010u;
    for (int i=0;i<4;i++) dest[i]=0xff000000u;
    if (gfx2_sw_alpha(&ex,&t) || dest[0]!=0xff404040u || dest[1]!=0xff404040u || dest[2]!=0xff101010u || t.w!=4) return 1;
    op.flags=0; op.fg.x=128; ex.clip.x=1; ex.clip.w=2;
    dest[0]=dest[1]=dest[2]=dest[3]=0xff000000u;
    if (gfx2_sw_alpha(&ex,&t) || dest[0]!=0xff000000u || dest[1]!=0xff202020u || dest[2]!=0xff080808u || t.x!=1) return 2;
    ex.sv.sync=&s; ex.dv.sync=&d; op.fg.x=255; op.flags=GFX2_F_ALPHA_PIXEL;
    dest[1]=dest[2]=0xff000000u;
    if (gfx2_sw_alpha(&ex,&t) || dest[1]!=0xff404040u || dest[2]!=0xff101010u) return 3;
    fail_read=1; if (gfx2_sw_alpha(&ex,&t)!=OS32_ERR_IO) return 4; fail_read=0;
    fail_write=1; if (gfx2_sw_alpha(&ex,&t)!=OS32_ERR_IO) return 5;
    return 0;
}
void _start(void) { int rc=probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); for (;;) {} }
