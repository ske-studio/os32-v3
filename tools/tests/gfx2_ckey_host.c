#include "gfx/v2/gfx2_sw.h"

u8 gfx2_sw_rowbuf[2][GFX2_SW_ROW_BYTES];
struct GfxSyncCtx { u8 *data; u32 pitch; int fail; };
int gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *o)
{ if (fmt != 1) return OS32_ERR_INVAL; o->cpp = 1; return 0; }
u32 gfx2_px_read(const u8 *p, u32 fmt) { (void)fmt; return *p; }
void gfx2_px_write(u8 *p, u32 fmt, u32 v) { (void)fmt; *p = (u8)v; }
u32 gfx2_to_argb(u32 fmt, u32 v, const u32 *pal) { (void)fmt; (void)pal; return v; }
int gfx2_from_argb(u32 fmt, u32 v, u32 *out) { (void)fmt; *out = v; return 0; }
int gfxsync_read_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, u8 *p, u32 pitch)
{ (void)h; (void)pitch; if (c->fail) return c->fail; for (u32 i=0;i<w;i++) p[i]=c->data[y*c->pitch+x+i]; return 0; }
int gfxsync_write_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, const u8 *p, u32 pitch)
{ (void)h; (void)pitch; if (c->fail) return c->fail; for (u32 i=0;i<w;i++) c->data[y*c->pitch+x+i]=p[i]; return 0; }
static int test(void)
{
    u8 s[6] = {1,2,1,3,4,5}, d[6] = {9,9,9,9,9,9};
    GFX2_Op op = { .flags=GFX2_F_SRCKEY, .skey={1,1}, .dw=3, .dh=1 };
    struct gfx2_surf a, b;
    struct gfx2_exec ex = { .op=&op, .src=&a, .dst=&b,
        .sv={.base=s,.pitch=3,.fmt=1}, .dv={.base=d,.pitch=3,.fmt=1}, .clip={0,0,3,1} };
    struct gfx2_rect t;
    if (gfx2_sw_ckey(&ex,&t) || d[0]!=9 || d[1]!=2 || d[2]!=9 || t.x!=1 || t.w!=1) return 1;
    op.flags=GFX2_F_DSTKEY; op.dkey.v=9; op.dkey.x=9; d[0]=8; d[1]=9; d[2]=8;
    if (gfx2_sw_ckey(&ex,&t) || d[0]!=8 || d[1]!=2 || d[2]!=8) return 2;
    op.flags=GFX2_F_SRCKEY|GFX2_F_DSTKEY; op.skey.v=1; op.skey.x=2; d[0]=d[1]=d[2]=9;
    if (gfx2_sw_ckey(&ex,&t) || d[0]!=9 || d[1]!=9 || d[2]!=9 || t.w) return 3;
    op.flags=GFX2_F_SRCKEY; op.skey.v=0; op.skey.x=0; op.dy=1; op.dh=1;
    ex.src=ex.dst=&a; ex.sv.base=ex.dv.base=s; ex.clip=(struct gfx2_rect){0,1,3,1};
    if (gfx2_sw_ckey(&ex,&t) || s[3]!=1 || s[4]!=2 || s[5]!=1) return 4;
    struct GfxSyncCtx sc={s,3,0}, dc={d,3,0};
    ex.src=&a; ex.dst=&b; ex.sv.sync=&sc; ex.dv.sync=&dc; ex.clip=(struct gfx2_rect){1,0,2,1}; op.dy=0;
    if (gfx2_sw_ckey(&ex,&t) || d[1]!=2 || d[2]!=1) return 5;
    sc.fail=OS32_ERR_IO;
    if (gfx2_sw_ckey(&ex,&t)!=OS32_ERR_IO) return 6;
    ex.clip.w=0;
    if (gfx2_sw_ckey(&ex,&t) || t.w || t.h) return 7;
    return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
