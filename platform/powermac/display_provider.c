#include "display_provider.h"
#include "drivers/video/of_fb/backend_of_fb.h"
#include "gfx/v2/gfx2_int.h"
#include "rh_observe.h"
#include "arch/ppc/kernel_boot.h"
#include "physmem.h"
#include "paging.h"
#include "kstring.h"

GfxBackend gfx_backend_unsupported = { .name = "none" };
static struct of_fb_desc saved;
static GfxSyncCtx *sync_ctx;

static void prepare(void)
{
    if (!of_fb_ready() && saved.display) (void)of_fb_bind(&saved);
}
static void shutdown(void)
{
    (void)gfx2_sync_publish(GFX2_KIND_OF_FB, 0, 0);
    gfxsync_unbind(sync_ctx);
    sync_ctx = 0;
    of_fb_unbind();
}
static void init(void)
{
    prepare();
    /* The v1 aliases must always name RAM. The OF backend alone transfers to
     * the device, with native-endian aligned stores; generic sync writes never
     * target this device (their byte/LE transfer policy is not suitable). */
    GfxSyncTarget t = {.va = saved.shadow, .bytes = saved.shadow_bytes,
                       .pitch = saved.shadow_pitch, .fmt = GFX2_FMT_INDEX8};
    if (!of_fb_ready()) return;
    if (!sync_ctx && gfxsync_bind("of_fb", &t, &sync_ctx)) { of_fb_unbind(); return; }
    (void)gfx2_sync_publish(GFX2_KIND_OF_FB, 0, sync_ctx);
}
int display_provider_pref(int pref) { (void)pref; return GFX_PREF_AUTO; }
GfxBackend *display_provider_select(int pref, int legacy200, display_select_mode_fn fn)
{
    (void)pref;
    prepare();
    if (of_fb_ready() && fn(&gfx_backend_of_fb, legacy200)) return &gfx_backend_of_fb;
    fn(&gfx_backend_unsupported, legacy200);
    return &gfx_backend_unsupported;
}
u32 display_provider_ledger(const GfxBackend *b)
{ return b == &gfx_backend_of_fb ? LEDGER_SF_OF_FB : 0; }
int gfx_cirrus_probed(void) { return 0; }
void display_provider_boot_reserve(int pref, void (*bind_client)(void))
{
    const struct pm_rh_fb *fb = pm_rh_full_framebuffer();
    const struct ppc_boot_devices *dev = ppc_boot_devices();
    struct ledger_surface sf = {0};
    u32 pfn, rid, bytes, pages, offset;
    (void)pref;
    if (saved.display || !fb || !dev || dev->fb_full_rc != PM_RH_FULL_OK ||
        !dev->fb_va || fb->depth != 8 ||
        (pm_rh_full_colors()->rc != PM_RH_COLOR_OK &&
         pm_rh_full_colors()->rc != PM_RH_COLOR_WRITTEN) || !fb->width || (fb->width & 3) ||
        fb->width > 65535 || !fb->height || fb->height > 65535 ||
        fb->stride < fb->width || fb->stride > 65535 || (fb->stride & 3) ||
        (dev->fb_va & 3) || (fb->pa & 3) ||
        (dev->fb_va & (PAGE_SIZE - 1)) != (fb->pa & (PAGE_SIZE - 1)) ||
        fb->height > PM_RH_FB_FULL_LIMIT / fb->stride) return;
    bytes = fb->stride * fb->height;
    offset = fb->pa & (PAGE_SIZE - 1);
    if (bytes > fb->bytes || offset > PM_RH_FB_FULL_LIMIT - bytes) return;
    pages = (offset + bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    struct ledger_resource r = {.decode_first = fb->pa / PAGE_SIZE,
        .decode_end = fb->pa / PAGE_SIZE + pages, .bus = LEDGER_BUS_FIXED,
        .width_basis = LEDGER_WB_GLUE_CONST};
    struct ledger_span sp = {.first = r.decode_first, .end = r.decode_end,
                             .kind = LEDGER_SPAN_MMIO};
    if (!ledger_resource_add(&r, &rid)) return;
    sp.res = rid;
    if (!ledger_reserve_set(LEDGER_OWNER_GFX, &sp, 1) ||
        !ledger_resource_set_map(rid, sp.first, sp.end)) return;
    if (!pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, (int)pages,
            MEM_POOL_BASE / PAGE_SIZE, pgalloc_arena_end(), LEDGER_TOP_DOWN, &pfn)) return;
    kmemset(P2V(pfn * PAGE_SIZE), 0, pages * PAGE_SIZE);
    sf = (struct ledger_surface){.first = pfn, .npages = pages,
        .width = fb->width, .height = fb->height, .pitch = fb->stride,
        .owner = LEDGER_OWNER_BOOT, .backing = LEDGER_SB_RAM,
        .backend = LEDGER_SF_OF_FB, .role = LEDGER_ROLE_CLIENT,
        .format = GFX_BB_PACKED8, .planes = 1, .cache = LEDGER_CACHE_WB,
        .perm_max = LEDGER_PERM_RW};
    if (!ledger_surface_create(&sf, 0)) {
        (void)pgalloc_free_n_owner(LEDGER_OWNER_BOOT, pfn, (int)pages);
        return;
    }
    bind_client();
    sf.first = sp.first; sf.owner = LEDGER_OWNER_GFX;
    sf.plane_offset[0] = offset;
    sf.backing = LEDGER_SB_MMIO; sf.role = LEDGER_ROLE_DISPLAY;
    sf.cache = LEDGER_CACHE_UC; sf.perm_max = LEDGER_PERM_NONE;
    if (!ledger_surface_create(&sf, 0)) return;
    saved = (struct of_fb_desc){.display = (volatile u8 *)dev->fb_va,
        .shadow = P2V(pfn * PAGE_SIZE), .width = fb->width, .height = fb->height,
        .pitch = fb->stride, .bytes = bytes, .shadow_pitch = fb->stride,
        .shadow_bytes = pages * PAGE_SIZE, .protected_rows = fb->height < PM_RH_FB_ROWS ? fb->height : PM_RH_FB_ROWS};
    gfx_backend_of_fb.prepare = prepare;
    gfx_backend_of_fb.init = init;
    gfx_backend_of_fb.shutdown = shutdown;
    (void)of_fb_bind(&saved);
}
u32 display_provider_display_records(u32 backend) { (void)backend; return 1; }
u32 display_provider_display_first(u32 plane)
{
    struct ledger_surface *s = ledger_surface_find(LEDGER_SF_OF_FB, LEDGER_ROLE_DISPLAY);
    return !plane && s ? s->first : 0;
}
void display_provider_each(void (*fn)(GfxBackend *, void *), void *arg)
{ if (of_fb_ready()) fn(&gfx_backend_of_fb, arg); }
int display_provider_client_before_init(const GfxBackend *b) { return b == &gfx_backend_of_fb; }
int display_provider_init_failed(GfxBackend *b) { (void)b; shutdown(); return -1; }
int display_provider_idle_safe(int pref) { (void)pref; return 0; }
void display_provider_prepare_failed(GfxBackend *b) { (void)b; shutdown(); }
void display_provider_raster_reset(void) { }
int display_provider_restore_fallback(GfxBackend *b) { (void)b; return 0; }
u32 display_provider_ver_kind(const GfxBackend *b) { (void)b; return GFX_VER_UNKNOWN; }
