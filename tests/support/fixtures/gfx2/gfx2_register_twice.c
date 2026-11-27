/* R5. Exercise the registry, GF stop result, and actual surface lifetime. */
#include "gfx2_int.h"
#include "redir_access.h"
#include "sys/dev/video/geforce/gf_v2.h"
#include "sys/dev/video/geforce/geforce_module.h"

extern struct gf_v2 gf_v2_inst;
const GeforceModuleContext *geforce_context;
static int stop_result, stops, released, warnings, revokes;
static void shutdown(void) { stops++; gf_v2_stopped(stop_result); }
static int flip_done(void *ctx, u32 head, u32 *retired) { return OS32_ERR_NOSYS; }
void gfx_policy_scan_changed(void) {}
static int stop(void *ctx) { return gf_v2_stop(&gf_v2_inst); }
int gf2_fault_check(struct gf2_chan *ch) { return GF2_EIO; }
int gf_v2_fence_idle(struct gf_v2 *g, u32 dl) { return GF2_EIO; }
int gf2_fence_emit(struct gf2_chan *ch, struct gf2_fence *f, u32 dl) { return GF2_EIO; }
int gf2_fence_wait(struct gf2_chan *ch, struct gf2_fence f, u32 dl) { return GF2_EIO; }
void gf_v2_gart_reset(struct gf_v2 *g) {}
static int release(void *ctx, struct gfx2_surf *s) { released++; return 0; }
static int heap(void *ctx, u32 *base, u32 *size, u32 *phys)
{
    *base = *size = *phys = 0;
    return 0;
}
const GfxBackendV2 gfx2_v1_shim = {.flip_done = flip_done, .stop = stop, .release = release, .heap_range = heap};
const GfxBackendV2 gfx2_lost_be = {0};
void gfx2_caps_refresh(struct gfx2_adapter *a) { a->caps.heads = 2; }
void gfx_policy_reeval(void) {}
void gfx2_quarantine_drain(struct gfx2_adapter *a) {}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner) { *owner = 1; return 1; }
void kprintf(u8 attr, const char *fmt, ...) { warnings++; }
int gfx2_heap_init(struct gfx2_heap *h, u32 base, u32 size) { return 0; }
int gfx2_unmap(struct gfx2_surf *s) { return 0; }
int gfx2_heap_free(struct gfx2_heap *h, u32 off) { return 0; }
int gfx2_gart_free(struct gfx2_adapter *a, struct gfx2_surf *s) { return OS32_ERR_INVAL; }
int gfx2_sysmem_free(struct gfx2_surf *s) { return OS32_ERR_INVAL; }
static struct ledger_surface gf_display = {.first=0x1234, .npages=1, .width=8, .height=8, .pitch=8, .backing=LEDGER_SB_VRAM};
static struct ledger_surface pc_display = {.first=0x5678, .npages=1, .width=8, .height=8, .pitch=8, .backing=LEDGER_SB_VRAM};
struct ledger_surface *ledger_surface_find(u32 backend, u32 role) {
    if (role != LEDGER_ROLE_DISPLAY) return 0;
    return backend == GFX2_KIND_GEFORCE ? &gf_display : &pc_display;
}
i32 gfx_screen_owner(void) { return 1; }
int caller_access_get_user(struct caller_access *c) { c->owner = 1; c->app_id = 1; return 1; }
int gfxsync_mode(const GfxSyncCtx *c) { return 0; }
int gfx2_lock_revoke(struct gfx2_surf *s) { revokes++; return 0; }
void *kmemset(void *dst, int value, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)dst)[i] = value;
    return dst;
}
void *memset(void *dst, int value, unsigned n) { return kmemset(dst, value, n); }

static GfxBackend gf = {.shutdown = shutdown}, pc98;
GfxBackend *g_backend;
#define DISPLAY_FALLBACK_BACKEND (&pc98)
#define GFX_HEIGHT 400
static int gfx_current_height, gfx_flip_enabled, gfx_display_page, gfx_started;
static int finishes, returns;
static u32 display_provider_ledger(const GfxBackend *be)
{ return be == &pc98 ? GFX2_KIND_PC98 : GFX2_KIND_GEFORCE; }
static void display_provider_each(void (*fn)(GfxBackend *, void *), void *arg) {}
int ring3_call_from_user(void) { return 0; }
int con_sink_is_enabled(void) { return 0; }
int appslot_gfx_owner(void) { return 1; }
static void display_provider_raster_reset(void) {}
static int display_provider_console_owns_scanout(void) { return 0; }
static int display_provider_restore_fallback(GfxBackend *be) { return be == &gf; }
static void display_provider_console_return(void) { returns++; }
static void gfx_reinit_surfaces(int finish) { finishes += finish; }
static void gfx_select_mode(GfxBackend *be, int legacy200) {}
static void gfx_bind_client(void) {}
#include "sys/hal/gfx/gfx_core_shutdown.inc"

static int run(void)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        if (gfx2_adapter_register(&gf, GFX2_KIND_GEFORCE, GFX2_AF_ACTIVE)) return 1;
        struct gfx2_adapter *a = gfx2_active();
        struct gfx2_surf *alias = &gfx2_surfs[0];
        struct gfx2_surf *scan = &gfx2_surfs[1], *pend = &gfx2_surfs[2];
        *scan = (struct gfx2_surf){.state=GFX2_ST_IDLE, .sid=1025, .pool=GFX2_POOL_VRAM};
        *pend = (struct gfx2_surf){.state=GFX2_ST_IDLE, .sid=1026, .pool=GFX2_POOL_VRAM};
        gfx2_scan_ref_swap(a, 1, scan);
        a->head[1].pend_sid = pend->sid;
        gfx2_scan_ref_get(pend);
        u32 gen = a->gen;
        if (gfx2_adapter_register(&gf, GFX2_KIND_GEFORCE, GFX2_AF_ACTIVE) ||
            gfx2_active() != a || a->gen != gen || stops != attempt ||
            scan->scan_ref != 1 || pend->scan_ref != 1) return 1;
        if (!(alias->flags & GFX2_SF_ALIAS) || alias->adapter != a->index) return 5;
        gfx2_scan_ref_swap(a, 0, alias);
        u32 old_sid = alias->sid, old_off = alias->gpu_off;
        stop_result = attempt ? OS32_ERR_IO : 0;
        if (attempt) {
            int before = revokes;
            for (u32 i = 3; i < GFX2_MAX_SURFACES; i++)
                gfx2_surfs[i].state = GFX2_ST_IDLE;
            if (gfx2_adapter_register(&pc98, GFX2_KIND_PC98, GFX2_AF_ACTIVE) != OS32_ERR_NOSPC ||
                warnings != 1) return 7;
            g_backend = &gf;
            gfx_started = 1;
            gfx_shutdown();
            if (finishes || returns != 1 || gfx_started || g_backend != &pc98) return 11;
            if (warnings != 2 || gfx2_active() != a || alias->scan_ref != 1 || alias->sid != old_sid ||
                alias->gpu_off != old_off) return 7;
            if (before != revokes) return 9;
            if (gfx2_surf_alias_update(alias, NULL) != OS32_ERR_BUSY) return 6;
            gfx2_surfs[3].state = GFX2_ST_FREE;
        }
        if (gfx2_adapter_register(&pc98, GFX2_KIND_PC98, GFX2_AF_ACTIVE) ||
            gfx2_active()->v1 != &pc98 || (a->flags & GFX2_AF_ACTIVE)) return 2;
        if (attempt) gfx2_flip_poll(a);
        if (!attempt) {
            if (a->head[0].scan_sid || alias->scan_ref || alias->sid == old_sid ||
                alias->adapter != gfx2_active()->index || alias->gpu_off == old_off) return 5;
            if (stops != 1 || a->gen != gen + 1 || scan->scan_ref || pend->scan_ref ||
                a->head[1].scan_sid || a->head[1].pend_sid ||
                gfx2_surf_destroy(scan) || gfx2_surf_destroy(pend) || released != 2) return 3;
        } else if (stops != 5 || a->gen != gen + 3 || a->head[1].scan_sid != scan->sid ||
                   a->head[1].pend_sid != pend->sid || scan->scan_ref != 1 ||
                   pend->scan_ref != 1 || a->state != GFX2_AD_LOST_HELD ||
                   gfx2_surf_destroy(scan) != OS32_ERR_BUSY ||
                   gfx2_surf_destroy(pend) != OS32_ERR_BUSY || released != 2) return 4;
        if (attempt && (gfx2_surfs[3].role != GFX2_ROLE_DISPLAY ||
                        gfx2_surfs[3].adapter != gfx2_active()->index ||
                        gfx2_surfs[3].gpu_off != pc_display.first * PAGE_SIZE)) return 8;
        if (attempt) {
            int rc;
            if (gfx2_surf_get(old_sid, 1, &rc) || rc != OS32_ERR_STALE) return 10;
        }
        if (attempt && (a->head[0].scan_sid != old_sid || alias->sid != old_sid ||
                        alias->scan_ref != 1 || alias->gpu_off != old_off ||
                        alias->adapter != a->index)) return 6;
    }
    return 0;
}
void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(run()));
    __builtin_unreachable();
}
