#include "gfx2_int.h"
static struct gfx2_adapter a;
static struct gfx2_surf surf;
static int qrc, mrc, calls, swaps, policy;
const GfxBackendV2 gfx2_v1_shim = {0};
static int conn(void *x, u32 i, GFX2_ConnectorInfo *c) { (void)x; (void)i; c->possible_heads=1; c->clone_mask=0; return 0; }
static int mode(void *x, u32 h, u32 f, u32 i, GFX2_ModeInfo *m) { (void)x; (void)h; if (f != 1 || i) return OS32_ERR_INVAL; m->id=7; m->fmt=1; m->w=640; m->h=480; return 0; }
static int set(void *x,u32 h,u32 cm,const GFX2_ModeInfo *m,const struct gfx2_surf *s) { (void)x;(void)h;(void)cm;(void)m;(void)s; calls++; return mrc; }
static const GfxBackendV2 be = { .enum_conn=conn, .enum_mode=mode, .mode_set=set };
struct gfx2_adapter *gfx2_adapter_get(u32 i) { return i ? 0 : &a; }
int gfx2_caller_get(struct gfx2_caller *c) { c->app_id=1; c->owner=2; return 0; }
i32 gfx_screen_owner(void) { return 1; }
struct gfx2_surf *gfx2_surf_get(u32 sid,u32 owner,int *rc) { (void)owner; *rc=OS32_ERR_STALE; return sid == 1 ? &surf : 0; }
int gfx2_surf_quiesce(struct gfx2_surf *s,u32 dl) { (void)s;(void)dl; return qrc; }
void gfx2_scan_ref_swap(struct gfx2_adapter *p,u32 h,struct gfx2_surf *s) { swaps++; p->head[h].scan_sid=s->sid; }
void gfx2_adapter_recovered(struct gfx2_adapter *p) { p->state=GFX2_AD_OK; }
void gfx_policy_scan_changed(void) { policy++; }
int gfx_fullscreen_owner(void) { return 0; }
void v1_bridge_rebind(void) {}
int gfx2_mode_set(u32,u32,u32,u32,u32);
static int test(void) {
 a.be=&be; a.flags=GFX2_AF_ACTIVE; a.heads=1; a.caps.connectors=1; a.gen=5;
 surf.sid=1; surf.usage=GFX2_USE_SCANOUT; surf.fmt=1; surf.w=640; surf.h=480;
 qrc=OS32_ERR_TIMEOUT;
 if (gfx2_mode_set(0,0,1,7,1)!=OS32_ERR_TIMEOUT || calls || swaps || policy || a.gen!=5) return 1;
 qrc=0; mrc=OS32_ERR_IO;
 if (gfx2_mode_set(0,0,1,7,1)!=OS32_ERR_IO || calls!=1 || swaps || policy || a.gen!=5) return 2;
 mrc=0;
 if (gfx2_mode_set(0,0,1,7,1) || swaps!=1 || policy!=1 || a.head[0].scan_sid!=1 || a.head[0].mode_id!=7 || a.head[0].conn_mask!=1) return 3;
 return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
