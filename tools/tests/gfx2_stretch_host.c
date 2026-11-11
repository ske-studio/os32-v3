#include "gfx2_sw.h"
#include "os32_kapi_shared.h"
static u8 src[2] = {10,20}, dst[4];
int gfx2_fmt_info(u32 f, struct gfx2_fmt_info *i) { (void)f; i->cpp=1; return 0; }
u32 gfx2_px_read(const u8 *p,u32 f) { (void)f; return *p; }
void gfx2_px_write(u8 *p,u32 f,u32 v) { (void)f; *p=(u8)v; }
u32 gfx2_to_argb(u32 f,u32 v,const u32 *p) { (void)f; (void)p; return v; }
int gfx2_from_argb(u32 f,u32 a,u32 *v) { (void)f; *v=a; return 0; }
u32 gfx2_yuv_argb(u8 y,u8 u,u8 v) { (void)u; (void)v; return y; }
int gfxsync_read_rows(GfxSyncCtx *c,u32 x,u32 y,u32 w,u32 h,u8 *p,u32 pitch) { (void)c;(void)y;(void)w;(void)h;(void)pitch; *p=src[x]; return 0; }
int gfxsync_write_rows(GfxSyncCtx *c,u32 x,u32 y,u32 w,u32 h,const u8 *p,u32 pitch) { (void)c;(void)y;(void)w;(void)h;(void)pitch; dst[x]=*p; return 0; }
static int probe(void) {
    GFX2_Op op = {0}; struct gfx2_exec ex = {0}; struct gfx2_rect t;
    op.sw=2; op.sh=1; op.dw=4; op.dh=1; ex.op=&op;
    ex.sv.base=src; ex.sv.pitch=2; ex.sv.fmt=GFX2_FMT_L8;
    ex.dv.base=dst; ex.dv.pitch=4; ex.dv.fmt=GFX2_FMT_L8;
    ex.clip.w=4; ex.clip.h=1;
    if (gfx2_sw_stretch(&ex,&t) || dst[0]!=10 || dst[1]!=10 || dst[2]!=20 || dst[3]!=20 || t.w!=4) return 1;
    op.flags=GFX2_F_FILTER;
    if (gfx2_sw_stretch(&ex,&t) || dst[0]!=10 || dst[1]!=13 || dst[2]!=18 || dst[3]!=20) return 2;
    ex.clip.x=1; ex.clip.w=2; dst[0]=0; dst[3]=0;
    ex.sv.sync=(GfxSyncCtx *)1; ex.dv.sync=(GfxSyncCtx *)1;
    if (gfx2_sw_stretch(&ex,&t) || dst[0]!=0 || dst[1]!=13 || dst[2]!=18 || dst[3]!=0 || t.x!=1) return 3;
    return 0;
}
void _start(void) { int rc=probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); for (;;) {} }
