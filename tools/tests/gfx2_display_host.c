#include "gfx2_int.h"
struct gfx2_surf gfx2_surfs[GFX2_MAX_SURFACES];
static struct gfx2_adapter adapter;
static int fail, calls, flip_ready;
static u32 sent_x, sent_y, sent_w, sent_h, palette_count;
struct gfx2_adapter *gfx2_adapter_get(u32 n) { return n ? NULL : &adapter; }
int gfx2_caller_get(struct gfx2_caller *c) { c->app_id = 1; c->owner = 1; return 0; }
i32 gfx_screen_owner(void) { return 1; }
int gfx2_sync_get(const struct gfx2_adapter *a, u32 h, GfxSyncCtx **c, u32 *mode)
{ (void)a; (void)h; *c = (GfxSyncCtx *)1; *mode = GFXSYNC_SHADOW; return 0; }
int gfxsync_write_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, const u8 *p, u32 pitch)
{ (void)c; (void)p; (void)pitch; calls++; sent_x=x; sent_y=y; sent_w=w; sent_h=h; return fail ? OS32_ERR_IO : 0; }
void gfx2_pal_set(u32 first, u32 count, const u32 *p) { (void)first; (void)p; palette_count += count; }
static int pal(void *c, u32 f, u32 n, const u32 *p)
{ (void)c; (void)f; (void)n; (void)p; return fail ? OS32_ERR_IO : 0; }
static int flip(void *c, u32 h, const struct gfx2_surf *s, u32 f, u32 dl)
{ (void)c; (void)h; (void)s; (void)f; (void)dl; return fail ? OS32_ERR_IO : 0; }
static int done(void *c, u32 h, u32 *retired)
{ (void)c; (void)h; *retired = flip_ready ? 1024 : 0; return flip_ready ? 0 : OS32_ERR_BUSY; }
struct gfx2_surf *gfx2_surf_get(u32 sid, u32 owner, int *rc)
{ (void)owner; *rc = 0; return sid == gfx2_surfs[1].sid ? &gfx2_surfs[1] : NULL; }
int gfx2_surf_quiesce(struct gfx2_surf *s, u32 dl) { (void)s; (void)dl; return 0; }
void gfx2_scan_ref_get(struct gfx2_surf *s) { s->scan_ref++; }
void gfx2_scan_ref_put(struct gfx2_surf *s) { s->scan_ref--; }
void gfx2_scan_ref_swap(struct gfx2_adapter *a, u32 h, struct gfx2_surf *s)
{ if (s) gfx2_scan_ref_get(s); if (a->head[h].scan_sid) gfx2_scan_ref_put(&gfx2_surfs[0]); a->head[h].scan_sid = s ? s->sid : 0; }
void gfx_policy_scan_changed(void) { }
static const GfxBackendV2 be = { .palette = pal, .flip = flip, .flip_done = done };
static u8 pixels[1024];
static int run(void)
{
    u32 rgb[7] = {0}; struct gfx2_surf *s = &gfx2_surfs[0];
    adapter.be=&be; adapter.heads=1; adapter.flags=GFX2_AF_ACTIVE;
    s->state=GFX2_ST_IDLE; s->flags=GFX2_SF_ALIAS; s->role=GFX2_ROLE_DISPLAY;
    s->w=16; s->h=16; s->fmt=GFX2_FMT_INDEX8; s->pitch=16; s->kva=pixels;
    s->dirty=(struct gfx2_rect){2,3,5,6};
    if (gfx2_palette(0,250,7,rgb)!=OS32_ERR_INVAL || palette_count) return 1;
    if (gfx2_palette(0,0,0,rgb)!=OS32_ERR_INVAL || palette_count) return 2;
    fail=1;
    if (gfx2_palette(0,0,1,rgb)!=OS32_ERR_IO || palette_count) return 3;
    if (gfx2_present(0,0,0,0,16,16)!=OS32_ERR_IO || s->dirty.w!=5) return 4;
    fail=0;
    if (gfx2_present(0,0,0,0,16,16) || calls!=2 || sent_x!=2 || sent_y!=3 || sent_w!=5 || sent_h!=6 || s->dirty.w) return 5;
    s->sid=1024; s->scan_ref=1; adapter.head[0].scan_sid=1024;
    gfx2_surfs[1].sid=1025; gfx2_surfs[1].state=GFX2_ST_IDLE;
    gfx2_surfs[1].usage=GFX2_USE_SCANOUT;
    fail=1;
    if (gfx2_flip(0,0,1025,0,1)!=OS32_ERR_IO || gfx2_surfs[1].scan_ref || adapter.head[0].pend_sid) return 6;
    fail=0;
    if (gfx2_flip(0,0,1025,0,1) || gfx2_surfs[1].scan_ref!=1 || s->scan_ref!=1) return 7;
    if (gfx2_flip(0,0,1025,0,1)!=OS32_ERR_BUSY) return 8;
    flip_ready=1; gfx2_flip_poll(&adapter);
    if (s->scan_ref || gfx2_surfs[1].scan_ref!=1 || adapter.head[0].scan_sid!=1025 || adapter.head[0].pend_sid) return 9;
    return 0;
}
void _start(void) { int rc=run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
