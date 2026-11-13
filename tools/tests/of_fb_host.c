/* R3: real OF backend/provider, with ledger/firmware dependencies replaced. */
#include "drivers/video/of_fb/backend_of_fb.h"
#include "gfx/v2/gfx2_int.h"
#include "display_provider.h"
#include "kernel/console/console_dual.h"
#include "platform/powermac/rh_observe.h"
#include "arch/ppc/kernel_boot.h"
#include "kstring.h"
#include "paging.h"

static int failed;
#define EXPECT(x) do { if (!(x)) { const char msg[] = "FAIL " #x "\n"; failed = 1; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg)-1) : "memory"); } } while (0)
static u8 ram[8192] __attribute__((aligned(4096)));
static u8 screen[8192] __attribute__((aligned(4096)));
static u8 pixels[4096] __attribute__((aligned(4096)));
static struct gfx2_adapter adapter;
static int active = 1, reg_count, bind_calls, alloc_calls, fail_display, reserve_calls;
static int firmware_ok = 1;
static struct pm_rh_fb firmware = {.pa = 0x90000000, .bytes = 96 * 72,
    .stride = 96, .width = 80, .height = 72, .depth = 8};
static struct ppc_boot_devices devices;
static struct ledger_surface records[2];
struct GfxSyncCtx { int used; };
static struct GfxSyncCtx sync;
const struct pm_rh_fb *pm_rh_full_framebuffer(void) { return firmware_ok ? &firmware : 0; }
const struct ppc_boot_devices *ppc_boot_devices(void) { return &devices; }
struct gfx2_adapter *gfx2_active(void) { return active ? &adapter : 0; }
struct gfx2_adapter *gfx2_adapter_get(u32 id) { return active && id == 0 ? &adapter : 0; }
void *kmemcpy(void *dst, const void *src, u32 n)
{ u8 *d = dst; const u8 *s = src; for (u32 i = 0; i < n; i++) d[i] = s[i]; return dst; }
void *kmemset(void *dst, int v, u32 n)
{ u8 *d = dst; for (u32 i = 0; i < n; i++) d[i] = v; return dst; }
int ledger_resource_add(const struct ledger_resource *r, u32 *id)
{ EXPECT(r->decode_first == firmware.pa / PAGE_SIZE); *id = 0; return 1; }
int ledger_reserve_set(u32 owner, const struct ledger_span *sp, u32 n)
{ EXPECT(owner == LEDGER_OWNER_GFX && n == 1 && sp->kind == LEDGER_SPAN_MMIO); reserve_calls++; return 1; }
int ledger_resource_set_map(u32 id, u32 first, u32 end)
{ EXPECT(id == 0 && first < end); return 1; }
u32 pgalloc_arena_end(void) { return 0x10000; }
int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 flags, u32 *out)
{ (void)first; (void)end; EXPECT(owner == LEDGER_OWNER_BOOT && n == 2 && flags == LEDGER_TOP_DOWN);
  alloc_calls++; *out = (u32)ram / PAGE_SIZE; return 1; }
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{ (void)owner; (void)pfn; (void)n; return 1; }
int ledger_surface_create(const struct ledger_surface *sf, u32 *sid)
{
    (void)sid;
    EXPECT(sf->backend == LEDGER_SF_OF_FB && sf->pitch == 96 && sf->planes == 1);
    if (sf->role == LEDGER_ROLE_DISPLAY) {
        EXPECT(bind_calls == 1 && sf->backing == LEDGER_SB_MMIO && sf->perm_max == LEDGER_PERM_NONE && sf->plane_offset[0] == 4);
        if (fail_display) return 0;
    } else EXPECT(sf->backing == LEDGER_SB_RAM && sf->perm_max == LEDGER_PERM_RW);
    records[reg_count++] = *sf;
    return 1;
}
struct ledger_surface *ledger_surface_find(u32 kind, u32 role)
{ for (int i = 0; i < reg_count; i++) if (kind == records[i].backend && role == records[i].role) return &records[i]; return 0; }
static void bind_client(void) { EXPECT(reg_count == 1); bind_calls++; }
int gfxsync_bind(const char *key, const GfxSyncTarget *t, GfxSyncCtx **out)
{ EXPECT(key[0] == 'o' && t->va == ram && t->va != screen); sync.used = 1; *out = &sync; return 0; }
void gfxsync_unbind(GfxSyncCtx *ctx) { if (ctx) ctx->used = 0; }
int gfx2_sync_publish(u32 kind, u32 head, GfxSyncCtx *ctx)
{ EXPECT(kind == GFX2_KIND_OF_FB && head == 0 && (!ctx || ctx == &sync)); return 0; }
extern GfxBackend gfx_backend_unsupported;
static int fallback, select_calls;
static int selected(GfxBackend *be, int legacy)
{
    EXPECT(legacy == 0);
    EXPECT(be == (fallback ? &gfx_backend_unsupported : &gfx_backend_of_fb));
    select_calls++;
    be->selected_mode = be->modes;
    return 1;
}
static void each(GfxBackend *be, void *arg) { EXPECT(be == &gfx_backend_of_fb); (*(int *)arg)++; }

static void raster(void)
{
    struct of_fb_desc d = {.display = screen, .shadow = ram, .width = 12, .height = 4,
        .pitch = 20, .bytes = 80, .shadow_pitch = 16, .shadow_bytes = 64,
        .protected_rows = 1};
    GFX_ScreenInfo info;
    kmemset(screen, 0xa5, sizeof(screen));
    for (u32 i = 0; i < sizeof(ram); i++) ram[i] = (u8)(i * 13 + 7);
    EXPECT(of_fb_bind(&d) == 0);
    EXPECT(gfx_backend_of_fb.bb_base == ram && gfx_backend_of_fb.bb_size == 64);
    EXPECT(!gfx_backend_of_fb.direct_ok && !gfx_backend_of_fb.direct_ops &&
           !gfx_backend_of_fb.fill_rect && !gfx_backend_of_fb.blit && !gfx_backend_of_fb.put);
    EXPECT(!gfx_backend_of_fb.query(&info) && info.bpp == 8 && !info.flags);
    gfx_backend_of_fb.present_rect(3, 1, 2, 1);
    for (u32 i = 0; i < 80; i++) EXPECT(screen[i] == (i >= 20 && i < 28 ? ram[16+i-20] : 0xa5));
    gfx_backend_of_fb.present_rect(-3, -2, 100, 100);
    for (u32 y = 0; y < 4; y++) for (u32 x = 0; x < 20; x++)
        EXPECT(screen[y*20+x] == (y && x < 12 ? ram[y*16+x] : 0xa5));
    u8 saved = screen[20];
    gfx_backend_of_fb.present_rect(0x7fffffff, 0, 0x7fffffff, 1);
    gfx_backend_of_fb.present_rect(-2147483647-1, 0, 1, 1);
    EXPECT(screen[20] == saved);
    EXPECT(!gfx_backend_of_fb.set_mode(1) && gfx_backend_of_fb.set_mode(2) == OS32_ERR_NOSYS);
    gfx_backend_of_fb.set_palette(0, 256, pixels);
    EXPECT(screen[20] == saved);
    d.pitch = 8; EXPECT(of_fb_bind(&d) == OS32_ERR_INVAL && !of_fb_ready());
    d.pitch = 20; d.bytes = 79; EXPECT(of_fb_bind(&d) == OS32_ERR_INVAL);
    d.bytes = 80; d.display = screen + 1; EXPECT(of_fb_bind(&d) == OS32_ERR_INVAL);
    d.display = screen; d.width = 11; EXPECT(of_fb_bind(&d) == OS32_ERR_INVAL);
    d.width = 12; EXPECT(!of_fb_bind(&d));

    adapter = (struct gfx2_adapter){.v1 = &gfx_backend_of_fb, .kind = GFX2_KIND_OF_FB};
    GFX2_ModeInfo m;
    GFX2_HeadInfo head;
    GFX2_ConnectorInfo conn;
    GFX2_Caps caps;
    EXPECT(!of_fb_v2.caps(&adapter, &caps) && caps.heads == 1 && caps.connectors == 1 && !caps.caps2d_hw);
    EXPECT(caps.fmt_scanout == (1u << GFX2_FMT_INDEX8));
    EXPECT(!of_fb_v2.enum_head(&adapter, 0, &head) && head.attached);
    EXPECT(of_fb_v2.enum_head(&adapter, 1, &head) == OS32_ERR_INVAL);
    EXPECT(!of_fb_v2.enum_conn(&adapter, 0, &conn));
    EXPECT(of_fb_v2.enum_conn(&adapter, 1, &conn) == OS32_ERR_INVAL);
    EXPECT(!of_fb_v2.enum_mode(&adapter, 0, GFX2_FMT_INDEX8, 0, &m));
    EXPECT(of_fb_v2.enum_mode(&adapter, 0, GFX2_FMT_RGB565, 0, &m) == OS32_ERR_INVAL);
    struct gfx2_surf s = {.sid = 5, .state = GFX2_ST_IDLE, .w = 12, .h = 4,
        .fmt = GFX2_FMT_INDEX8, .pool = GFX2_POOL_SYSMEM, .pitch = 16, .bytes = 64, .kva = pixels};
    for (u32 i = 0; i < sizeof(pixels); i++) pixels[i] = (u8)(i+3);
    EXPECT(!of_fb_v2.mode_set(&adapter, 0, 1, &m, &s));
    EXPECT(screen[20] == pixels[16] && screen[0] == 0xa5 && screen[32] == 0xa5);
    pixels[16] = 55; EXPECT(!of_fb_v2.flip(&adapter, 0, &s, 0, 0) && screen[20] == 55);
    EXPECT(of_fb_v2.flip(&adapter, 0, &s, GFX2_FLIP_VBLANK, 0) == OS32_ERR_NOSYS);
    m.w++; EXPECT(of_fb_v2.mode_set(&adapter, 0, 1, &m, &s) == OS32_ERR_NOSYS); m.w--;
    s.state = GFX2_ST_LOST; pixels[16] = 77;
    EXPECT(of_fb_v2.flip(&adapter, 0, &s, 0, 0) != 0 && screen[20] == 55);
    s.state = GFX2_ST_FREE; EXPECT(of_fb_v2.flip(&adapter, 0, &s, 0, 0) != 0);
    s.state = GFX2_ST_IDLE; adapter.state = GFX2_AD_LOST;
    EXPECT(of_fb_v2.flip(&adapter, 0, &s, 0, 0) == OS32_ERR_STALE);
    adapter.state = GFX2_AD_OK;
    EXPECT(!of_fb_v2.stop(&adapter) && !of_fb_ready());
    EXPECT(of_fb_v2.flip(&adapter, 0, &s, 0, 0) == OS32_ERR_STALE && screen[20] == 55);
    EXPECT(!of_fb_bind(&d)); EXPECT(!of_fb_v2.flip(&adapter, 0, &s, 0, 0) && screen[20] == 77);
    u32 base, size, phys, done, fail;
    EXPECT(!of_fb_v2.heap_range(&adapter, &base, &size, &phys) && !base && !size && !phys);
    EXPECT(of_fb_v2.palette(&adapter, 0, 1, 0) == OS32_ERR_NOSYS);
    EXPECT(of_fb_v2.check(&adapter, 0) == OS32_ERR_NOSYS);
    EXPECT(!of_fb_v2.fence_state(&adapter, &done, &fail) && !done && !fail);
    EXPECT(of_fb_v2.fence_wait(&adapter, 1, 0) == OS32_ERR_NOSYS);
    of_fb_unbind();
}
static void provider(void)
{
    int n = 0;
    struct con_out out;
    devices.fb_va = (u32)screen; devices.fb_full_rc = PM_RH_FULL_OK;
    /* Firmware fixture is 72 rows but only last 8 are drawable; buffers have
     * two pages to match the full descriptor and pitch padding. */
    EXPECT(display_provider_pref(GFX_PREF_PEGC) == GFX_PREF_AUTO);
    EXPECT(display_provider_display_records(0) == 1 && display_provider_display_records(1) == 1);
    firmware_ok = 0; display_provider_boot_reserve(0, bind_client);
    EXPECT(!of_fb_ready() && !alloc_calls);
    fallback = 1;
    EXPECT(display_provider_select(0, 0, selected) == &gfx_backend_unsupported && select_calls == 1);
    fallback = 0;
    firmware_ok = 1; firmware.depth = 32;
    display_provider_boot_reserve(0, bind_client); EXPECT(!of_fb_ready() && !alloc_calls);
    firmware.depth = 8;
    firmware.pa += 4; devices.fb_va += 4;
    display_provider_each(each, &n); EXPECT(n == 0);
    EXPECT(display_dual_outputs(&out, 1) == 1 && !out.live && out.role == CON_OUT_TEXT);
    display_provider_boot_reserve(0, bind_client);
    EXPECT(of_fb_ready() && reg_count == 2 && bind_calls == 1 && reserve_calls == 1);
    EXPECT(display_provider_ledger(&gfx_backend_of_fb) == LEDGER_SF_OF_FB);
    EXPECT(display_provider_select(0, 0, selected) == &gfx_backend_of_fb);
    display_provider_each(each, &n); EXPECT(n == 1);
    gfx_backend_of_fb.init(); EXPECT(sync.used);
    EXPECT(display_provider_client_before_init(&gfx_backend_of_fb));
    EXPECT(!display_provider_idle_safe(0));
    kmemset(screen, 0xa5, sizeof(screen));
    ram[64*96] = 31;
    gfx_backend_of_fb.present_rect(0, 64, 1, 1);
    EXPECT(screen[4+64*96] == 31 && screen[4] == 0xa5 && screen[4+64*96+80] == 0xa5);
    EXPECT(display_dual_outputs(&out, 1) == 1 && out.live && out.role == CON_OUT_TEXT);
    adapter.state = GFX2_AD_LOST;
    EXPECT(display_dual_outputs(&out, 1) == 1 && !out.live);
    adapter.state = GFX2_AD_OK;
    gfx_backend_of_fb.shutdown(); EXPECT(!of_fb_ready() && !sync.used);
    gfx_backend_of_fb.prepare(); EXPECT(of_fb_ready());
    gfx_backend_of_fb.init(); EXPECT(sync.used);
    EXPECT(display_provider_init_failed(&gfx_backend_of_fb) < 0 && !of_fb_ready());
}
void _start(void)
{
    raster(); provider();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failed) : "memory");
    __builtin_unreachable();
}
