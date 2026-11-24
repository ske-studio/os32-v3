/* Scroll expectations: docs/KAPI_SPEC.md, gfx_hardware_scroll, and DESIGN.final §4. */
/* PPC_KAPI_GFX_REST_DESIGN 5c §2-5, A3 and A8: transfer accounting and fixed mode. */
/* Expectations: characterization of the original word-copy behavior and
 * ppc-offb-fast PACK (2026-11-15): alignment must not change any output byte. */
/* R3: real OF backend/provider, with ledger/firmware dependencies replaced. */
#include "drivers/video/of_fb/backend_of_fb.h"
#include "gfx/v2/gfx2_int.h"
#include "display_provider.h"
#include "redir_access.h"
#include "kernel/console/console_dual.h"
#include "platform/powermac/rh_observe.h"
#include "arch/ppc/kernel_boot.h"
#include "kstring.h"
#include "paging.h"
#include "gfx/palette.h"
#include "platform/powermac/display_text.h"

GfxCounters gfx_counters;
GfxBackend *g_backend = &gfx_backend_of_fb;
static int gfx_started;
int gfx_current_height, gfx_flip_enabled, gfx_display_page;
u8 *bb_b;
#include "gfx/gfx_core_api.inc"

/* DESIGN.final §2/§4: only committed logical rows may reach scanout. */
static u8 frame_storage[64 * PAGE_SIZE] __attribute__((aligned(4096)));
extern int vram_scroll_y;
int gfx_is_started(void) { return gfx_started; }
static unsigned frame_pages;
static int allow_palette;
static int palette_allowed(void) { return allow_palette; }
static int current_app = 2;
static int wm_actor;
int appslot_present_owner(void) { return wm_actor ? wm_actor : current_app; }
int appslot_cur(void) { return current_app; }
static int failed;
#define EXPECT(x) do { if (!(x)) { const char msg[] = "FAIL " #x "\n"; failed = 1; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg)-1) : "memory"); } } while (0)
static u8 ram[8192] __attribute__((aligned(4096)));
static u8 screen[8192] __attribute__((aligned(4096)));
static u8 pixels[4096] __attribute__((aligned(4096)));
#define adapter gfx2_adapters[0]
static int reg_count, bind_calls, alloc_calls, fail_display, reserve_calls;
static int firmware_ok = 1;
static struct pm_rh_fb firmware = {.pa = 0x90000000, .bytes = 96 * 72,
    .stride = 96, .width = 80, .height = 72, .depth = 8};
static struct ppc_boot_devices devices;
static struct ledger_surface records[2];
struct GfxSyncCtx { int used; };
static struct GfxSyncCtx sync;
const struct pm_rh_fb *pm_rh_full_framebuffer(void) { return firmware_ok ? &firmware : 0; }
const struct ppc_boot_devices *ppc_boot_devices(void) { return &devices; }
const GfxBackendV2 gfx2_v1_shim = {0}, gfx2_lost_be = {0};
int module_text_contains(const struct module_record *m, u32 p) { (void)m; (void)p; return 1; }
int ledger_owner_new(u32 kind, u32 parent, const char *name, u32 *out)
{ (void)kind; (void)parent; (void)name; *out = 1; return 1; }
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
int gfx2_heap_init(struct gfx2_heap *h, u32 base, u32 size)
{ (void)h; EXPECT(!base && !size); return 0; }
struct gfx2_surf gfx2_surfs[GFX2_MAX_SURFACES];
i32 gfx_screen_owner(void) { return current_app; }
int caller_access_get_user(struct caller_access *c)
{ *c = (struct caller_access){ .app_id = current_app, .owner = current_app }; return 1; }
struct gfx2_surf *gfx2_surf_get(u32 sid, u32 owner, int *rc)
{
    EXPECT(sid == 1 && owner == (u32)current_app);
    *rc = 0;
    return &gfx2_surfs[1];
}
int gfx2_surf_quiesce(struct gfx2_surf *s, u32 dl)
{ EXPECT(s == &gfx2_surfs[1] && !dl); return 0; }
void gfx2_scan_ref_get(struct gfx2_surf *s) { EXPECT(s == &gfx2_surfs[1]); }
void gfx2_scan_ref_put(struct gfx2_surf *s) { EXPECT(s == &gfx2_surfs[1]); }
void gfx2_scan_ref_swap(struct gfx2_adapter *a, u32 h, struct gfx2_surf *s)
{ a->head[h].scan_sid = s ? s->sid : 0; }
void gfx_policy_scan_changed(void) { }
void gfx2_adapter_recovered(struct gfx2_adapter *a) { a->state = GFX2_AD_OK; }
void v1_bridge_rebind(void) { }
/* P6: OF enter stops the text terminal through display_text_hide; no terminal is attached here. */
int pm_console_park(void);
int pm_console_park(void) { return 0; }
static struct pm_rh_colors colors;
const struct pm_rh_colors *pm_rh_full_colors(void) { return &colors; }
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
{
  if (owner == LEDGER_OWNER_KERNEL) {
      EXPECT(!frame_pages && n > 0);
      if ((unsigned)n > sizeof(frame_storage) / PAGE_SIZE) return 0;
      frame_pages = n;
      *out = (u32)frame_storage / PAGE_SIZE;
      return 1;
  }
  (void)first; (void)end; EXPECT(owner == LEDGER_OWNER_BOOT && n == 2 && flags == LEDGER_TOP_DOWN);
  alloc_calls++; *out = (u32)ram / PAGE_SIZE; return 1; }
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    if (owner == LEDGER_OWNER_KERNEL) {
        EXPECT(pfn == (u32)frame_storage / PAGE_SIZE && (unsigned)n == frame_pages);
        frame_pages = 0;
    }
    return 1;
}
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

/* Byte oracle: original clipping and edge-word coverage, without word loads. */
static void copy_characterization(void)
{
    static u8 expected[8192];
    static const int rects[][4] = {
        {0, 0, 36, 4}, {3, 1, 2, 1}, {35, 3, 20, 20},
        {-3, -2, 9, 5}, {-3, -2, 100, 100}, {0, 0, 1, 1},
        {4, 0, 32, 4}, {0, 0, 0, 4}, {0, 0, 36, -1},
        {0x7fffffff, 0, 0x7fffffff, 1},
        {-2147483647-1, 0, 0x7fffffff, 1},
        {-10, -10, 0x7fffffff, 0x7fffffff},
        {0, 0x7fffffff, 1, 0x7fffffff},
        {0, -2147483647-1, 1, 0x7fffffff},
    };
    for (u32 offset = 0; offset < 4; offset++)
    for (u32 pitch = 40; pitch <= 43; pitch++)
    for (u32 protected = 0; protected <= 4; protected++) {
        struct of_fb_desc d = {.display = screen + 4, .shadow = ram + offset,
            .width = 36, .height = 4, .pitch = 44, .bytes = 176,
            .shadow_pitch = pitch, .shadow_bytes = pitch * 4,
            .protected_rows = protected};
        EXPECT(!of_fb_bind(&d));
        for (u32 i = 0; i < sizeof(ram); i++) ram[i] = (u8)(i * 13 + 7);
        for (u32 r = 0; r < sizeof(rects)/sizeof(rects[0]); r++) {
            int x = rects[r][0], y = rects[r][1];
            int w = rects[r][2], h = rects[r][3];
            kmemset(screen, 0xa5, sizeof(screen));
            kmemset(expected, 0xa5, sizeof(expected));
            /* Membership uses wide endpoints, independently of product loops. */
            long long right = (long long)x + w, bottom = (long long)y + h;
            if (w > 0 && h > 0 && right > 0 && bottom > 0 && x < 36 && y < 4) {
                int left = x < 0 ? 0 : x;
                int last = right > 36 ? 36 : (int)right;
                for (u32 row = protected; row < 4; row++)
                for (u32 col = 0; col < 36; col++) {
                    if ((long long)row >= y && (long long)row < bottom &&
                        col / 4 >= (u32)left / 4 && col / 4 <= (u32)(last - 1) / 4)
                        expected[4 + row * 44 + col] = ram[offset + row * pitch + col];
                }
            }
            EXPECT(!of_fb_copy(ram + offset, pitch, pitch * 4, x, y, w, h));
            for (u32 i = 0; i < sizeof(screen); i++) EXPECT(screen[i] == expected[i]);
            /* Also exercise the bound shadow/pitch via the public present hook. */
            kmemset(screen, 0xa5, sizeof(screen));
            gfx_backend_of_fb.present_rect(x, y, w, h);
            for (u32 i = 0; i < sizeof(screen); i++) EXPECT(screen[i] == expected[i]);
        }
    }
    of_fb_unbind();
}

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
    gfx_counters = (GfxCounters){0};
    gfx_backend_of_fb.present_rect(3, 1, 2, 1);
    EXPECT(gfx_counters.present_bytes == 8 && gfx_counters.commits == 1);
    EXPECT(!DISPLAY_V2_SHADOW_PUSH(3, 0, 2, 1));
    EXPECT(gfx_counters.present_bytes == 8 && gfx_counters.commits == 2);
    EXPECT(of_fb_present(0, 1, 0, 1) == OS32_ERR_INVAL);
    EXPECT(of_fb_copy(0, 16, 64, 0, 1, 1, 1) == OS32_ERR_INVAL);
    EXPECT(gfx_counters.present_bytes == 8 && gfx_counters.commits == 2);
    for (u32 i = 0; i < 80; i++) EXPECT(screen[i] == (i >= 20 && i < 28 ? ram[16+i-20] : i >= 20 && i % 20 < 12 ? 0 : 0xa5));
    gfx_backend_of_fb.present_rect(-3, -2, 100, 100);
    for (u32 y = 0; y < 4; y++) for (u32 x = 0; x < 20; x++)
        EXPECT(screen[y*20+x] == (y && x < 12 ? ram[y*16+x] : 0xa5));
    EXPECT(gfx_counters.present_bytes == 44 && gfx_counters.commits == 3);
    EXPECT(!of_fb_copy(ram, 16, 64, 9, 0, 20, 8));
    EXPECT(gfx_counters.present_bytes == 56 && gfx_counters.commits == 3);
    of_fb_unbind();
    EXPECT(of_fb_present(0, 1, 1, 1) == OS32_ERR_STALE);
    EXPECT(DISPLAY_V2_SHADOW_PUSH(0, 1, 1, 1) == OS32_ERR_IO);
    EXPECT(gfx_counters.present_bytes == 56 && gfx_counters.commits == 3);
    EXPECT(!gfx_counters.hw_ops && !gfx_counters.io_accesses);
    EXPECT(!of_fb_bind(&d));
    u8 saved = screen[20];
    gfx_backend_of_fb.present_rect(0x7fffffff, 0, 0x7fffffff, 1);
    gfx_backend_of_fb.present_rect(-2147483647-1, 0, 1, 1);
    EXPECT(screen[20] == saved);
    EXPECT(!gfx_backend_of_fb.set_mode(1) && gfx_backend_of_fb.set_mode(2) == OS32_ERR_NOSYS);
    gfx_backend_of_fb.set_palette(0, 256, pixels);
    EXPECT(screen[20] == saved);
    static const u8 clut[256][3] = {[42] = {255, 0, 0}};
    const u8 red[3] = {15, 0, 0};
    u8 r, g, b;
    d.clut = clut;
    d.palette_allowed = palette_allowed;
    EXPECT(!of_fb_bind(&d));
    EXPECT(!gfx_backend_of_fb.query(&info) && info.lease_first == 16 && info.lease_count == 240);
    ram[16] = 5;
    gfx_backend_of_fb.set_palette(5, 1, red);
    EXPECT(screen[20] == saved);
    gfx_set_palette_hal(5, 15, 0, 0);
    EXPECT(gfx2_pal[5] == 0 && screen[20] == saved);
    EXPECT(gfx_lease_palette(16, 1, red) == OS32_ERR_BUSY && gfx2_pal[16] == 0);
    EXPECT(!of_fb_present(0, 1, 4, 1));
    allow_palette = 1;
    EXPECT(!gfx_lease_palette(16, 1, red) && gfx2_pal[16] == 0xff0000);
    gfx_set_palette_hal(5, 15, 0, 0);
    EXPECT(gfx2_pal[5] == 0xff0000);
    palette_get(5, &r, &g, &b);
    EXPECT(r == 15 && g == 0 && b == 0);
    EXPECT(screen[20] == 42 && ram[16] == 5 && screen[21] == ram[17]);
    EXPECT(screen[0] == 0xa5 && screen[32] == 0xa5);
    ram[16] = 97; /* uncommitted */
    gfx_counters = (GfxCounters){0};
    of_fb_scroll(3);
    EXPECT(screen[40] == 42 && gfx_counters.present_bytes == 36 && !gfx_counters.commits);
    of_fb_scroll(3);
    EXPECT(gfx_counters.present_bytes == 36);
    ram[0] = 5;
    ram[1] = 81;
    EXPECT(!of_fb_present(0, 0, 1, 1) && screen[20] == 42);
    EXPECT(gfx_counters.present_bytes == 40 && gfx_counters.commits == 1);
    const u8 black[3] = {0, 0, 0};
    EXPECT(!of_fb_palette(5, 1, black) && screen[40] == 0 && screen[20] == 0);
    EXPECT(screen[21] == 81 && gfx_counters.present_bytes == 76 && gfx_counters.commits == 2);
    EXPECT(!of_fb_palette(5, 1, black));
    EXPECT(gfx_counters.present_bytes == 76 && gfx_counters.commits == 2);
    vram_scroll_y = 3;
    EXPECT(of_fb_replace(ram, 8, 64) == OS32_ERR_INVAL && vram_scroll_y == 3);
    EXPECT(gfx_counters.present_bytes == 76 && gfx_counters.commits == 2);
    EXPECT(!of_fb_replace(ram, 16, 64) && !vram_scroll_y && screen[20] == 97);
    EXPECT(gfx_counters.present_bytes == 112 && gfx_counters.commits == 2);
    d.clut = 0;
    d.pitch = 8; EXPECT(of_fb_bind(&d) == OS32_ERR_INVAL && !of_fb_ready());
    d.pitch = 20; d.bytes = 79; EXPECT(of_fb_bind(&d) == OS32_ERR_INVAL);
    d.bytes = 80; d.display = screen + 1; EXPECT(of_fb_bind(&d) == OS32_ERR_INVAL);
    d.display = screen; d.width = 11; EXPECT(of_fb_bind(&d) == OS32_ERR_INVAL);
    d.width = 12; EXPECT(!of_fb_bind(&d));

    EXPECT(!gfx2_adapter_register(&gfx_backend_of_fb, GFX2_KIND_OF_FB, GFX2_AF_ACTIVE));
    EXPECT(adapter.ctx == &gfx_backend_of_fb && adapter.caps.heads == 1);
    GFX2_ModeInfo m;
    GFX2_HeadInfo head;
    GFX2_ConnectorInfo conn;
    GFX2_Caps caps;
    EXPECT(!adapter.be->caps(adapter.ctx, &caps) && caps.heads == 1 && caps.connectors == 1 && !caps.caps2d_hw);
    EXPECT(caps.fmt_scanout == (1u << GFX2_FMT_INDEX8));
    EXPECT(!adapter.be->enum_head(adapter.ctx, 0, &head) && head.attached);
    EXPECT(adapter.be->enum_head(adapter.ctx, 1, &head) == OS32_ERR_INVAL);
    EXPECT(!adapter.be->enum_conn(adapter.ctx, 0, &conn));
    EXPECT(adapter.be->enum_conn(adapter.ctx, 1, &conn) == OS32_ERR_INVAL);
    EXPECT(!adapter.be->enum_mode(adapter.ctx, 0, GFX2_FMT_INDEX8, 0, &m));
    EXPECT(adapter.be->enum_mode(adapter.ctx, 0, GFX2_FMT_RGB565, 0, &m) == OS32_ERR_INVAL);
    struct gfx2_surf s = {.sid = 5, .state = GFX2_ST_IDLE, .w = 12, .h = 4,
        .fmt = GFX2_FMT_INDEX8, .pool = GFX2_POOL_SYSMEM, .pitch = 16, .bytes = 64, .kva = pixels};
    for (u32 i = 0; i < sizeof(pixels); i++) pixels[i] = (u8)(i+3);
    vram_scroll_y = 2; of_fb_scroll(2);
    EXPECT(!adapter.be->mode_set(adapter.ctx, 0, 1, &m, &s));
    EXPECT(!vram_scroll_y);
    EXPECT(screen[20] == pixels[16] && screen[0] == 0xa5 && screen[32] == 0xa5);
    vram_scroll_y = 3; of_fb_scroll(3);
    pixels[16] = 55; EXPECT(!adapter.be->flip(adapter.ctx, 0, &s, 0, 0) && screen[20] == 55);
    EXPECT(!vram_scroll_y);
    EXPECT(adapter.be->flip(adapter.ctx, 0, &s, GFX2_FLIP_VBLANK, 0) == OS32_ERR_NOSYS);
    m.id++; EXPECT(adapter.be->mode_set(adapter.ctx, 0, 1, &m, &s) == OS32_ERR_NOSYS); m.id--;
    m.h++; EXPECT(adapter.be->mode_set(adapter.ctx, 0, 1, &m, &s) == OS32_ERR_NOSYS); m.h--;
    m.w++; EXPECT(adapter.be->mode_set(adapter.ctx, 0, 1, &m, &s) == OS32_ERR_NOSYS); m.w--;
    s.state = GFX2_ST_LOST; pixels[16] = 77;
    EXPECT(adapter.be->flip(adapter.ctx, 0, &s, 0, 0) != 0 && screen[20] == 55);
    s.state = GFX2_ST_FREE; EXPECT(adapter.be->flip(adapter.ctx, 0, &s, 0, 0) != 0);
    s.state = GFX2_ST_IDLE; adapter.state = GFX2_AD_LOST;
    EXPECT(adapter.be->flip(adapter.ctx, 0, &s, 0, 0) == OS32_ERR_STALE);
    adapter.state = GFX2_AD_OK;
    EXPECT(!adapter.be->stop(&adapter) && !of_fb_ready());
    EXPECT(adapter.be->flip(adapter.ctx, 0, &s, 0, 0) == OS32_ERR_STALE && screen[20] == 55);
    EXPECT(!of_fb_bind(&d)); EXPECT(!adapter.be->flip(adapter.ctx, 0, &s, 0, 0) && screen[20] == 77);
    u32 base, size, phys, done, fail;
    EXPECT(!adapter.be->heap_range(adapter.ctx, &base, &size, &phys) && !base && !size && !phys);
    EXPECT(adapter.be->palette(adapter.ctx, 0, 1, 0) == OS32_ERR_NOSYS);
    EXPECT(adapter.be->check(adapter.ctx, 0) == OS32_ERR_NOSYS);
    EXPECT(!adapter.be->fence_state(adapter.ctx, &done, &fail) && !done && !fail);
    EXPECT(adapter.be->fence_wait(adapter.ctx, 1, 0) == OS32_ERR_NOSYS);
    of_fb_unbind();
}
/* Compare the entire firmware mapping, including both guards and every pitch gap. */
static void scanout(const u8 *expected, unsigned offset)
{
    for (unsigned y = 0; y < 72; y++)
        for (unsigned x = 0; x < 96; x++) {
            u8 color = y < 64 || x >= 80 ? 0xa5 : expected[((y + offset) % 72) * 80 + x];
            EXPECT(screen[4 + y * 96 + x] == color);
        }
    EXPECT(screen[3] == 0xa5 && screen[4 + 72 * 96] == 0xa5);
}

static void scroll_frames(void)
{
    static u8 expected[72 * 80];
    const int deltas[] = {1, -2, 72, -72, -2147483647-1, 2147483647};
    gfx_current_height = 72;
    bb_b = ram;
    gfx_started = 1;
    DISPLAY_V1_PUBLIC_INIT_OK();
    for (int pass = 0; pass < 4; pass++) {
        if (pass < 2) {
            gfx_backend_of_fb.shutdown();
            kmemset(screen, 0xa5, sizeof(screen));
            gfx_backend_of_fb.prepare();
            gfx_backend_of_fb.init();
            DISPLAY_V1_PUBLIC_INIT_OK();
        } else {
            gfx_counters = (GfxCounters){0};
            gfx_backend_of_fb.enter();
            EXPECT(!gfx_counters.commits && gfx_counters.present_bytes == 640);
        }
        kmemset(expected, 0, sizeof(expected));
        scanout(expected, 0);
        unsigned offset = 0;
        if (pass % 2 == 0) {
            gfx_hardware_scroll(1);
            offset = 1;
            scanout(expected, offset);
        }
        /* The rounded edge word straddles the physical wrap between rows. */
        for (unsigned y = 0; y < 2; y++)
            for (unsigned x = 0; x < 8; x++)
                expected[y * 80 + x] = ram[y * 96 + x] = 0x80 + y + x;
        gfx_counters = (GfxCounters){0};
        EXPECT(!of_fb_present(3, 0, 2, 2));
        scanout(expected, offset);
        EXPECT(gfx_counters.commits == 1 && gfx_counters.present_bytes == offset * 8);
        EXPECT(!of_fb_present(81, 0, 3, 1));
        EXPECT(gfx_counters.commits == 2 && gfx_counters.present_bytes == offset * 8);
        for (unsigned y = 0; y < 72; y++)
            for (unsigned x = 0; x < 80; x++)
                expected[y * 80 + x] = ram[y * 96 + x] = 1 + y + x;
        EXPECT(!of_fb_present(0, 0, 80, 72));
        kmemset(ram, 0xee, 72 * 96);
        for (unsigned i = 0; i < sizeof(deltas) / sizeof(deltas[0]); i++) {
            unsigned magnitude = deltas[i] < 0 ? 0u - (unsigned)deltas[i] : (unsigned)deltas[i];
            unsigned step = magnitude % 72;
            offset = (offset + (deltas[i] < 0 ? 72 - step : step)) % 72;
            gfx_counters = (GfxCounters){0};
            gfx_hardware_scroll(deltas[i]);
            scanout(expected, offset);
            EXPECT(!gfx_counters.commits && gfx_counters.present_bytes == (step ? 640u : 0u));
        }
    }
    gfx_started = 0;
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
    display_provider_boot_reserve(0, bind_client);
    EXPECT(!of_fb_ready() && !alloc_calls);
    colors.rc = PM_RH_COLOR_OK;
    firmware.pa += 4; devices.fb_va += 4;
    display_provider_each(each, &n); EXPECT(n == 0);
    EXPECT(display_dual_outputs(&out, 1) == 1 && !out.live && out.role == CON_OUT_TEXT);
    vram_scroll_y = 3;
    display_provider_boot_reserve(0, bind_client);
    EXPECT(of_fb_ready() && reg_count == 2 && bind_calls == 1 && reserve_calls == 1);
    EXPECT(display_provider_ledger(&gfx_backend_of_fb) == LEDGER_SF_OF_FB);
    EXPECT(display_provider_select(0, 0, selected) == &gfx_backend_of_fb);
    display_provider_each(each, &n); EXPECT(n == 1);
    gfx_backend_of_fb.init(); EXPECT(sync.used);
    gfx_started = 1;
    gfx_current_height = 72;
    gfx_counters = (GfxCounters){0};
    kmemset(screen, 0xa5, sizeof(screen));
    gfx_backend_of_fb.enter();
    EXPECT(!vram_scroll_y && !gfx_counters.commits && gfx_counters.present_bytes == 640);
    static const u8 blank[72 * 80];
    scanout(blank, 0);
    EXPECT(display_provider_client_before_init(&gfx_backend_of_fb));
    EXPECT(!display_provider_idle_safe(0));
    kmemset(screen, 0xa5, sizeof(screen));
    ram[64*96] = 31;
    gfx_backend_of_fb.present_rect(0, 64, 1, 1);
    EXPECT(screen[4+64*96] == 31 && screen[4] == 0xa5 && screen[4+64*96+80] == 0xa5);
    EXPECT(display_dual_outputs(&out, 1) == 1 && !out.live);
    u32 key = display_dual_key();
    pm_console_text_state(1, 0, 1, adapter.gen);
    EXPECT(!pm_console_text_ready());
    pm_console_text_state(0, 1, 1, adapter.gen);
    EXPECT(!pm_console_text_ready());
    pm_console_text_state(1, 1, 1, adapter.gen);
    EXPECT(display_dual_key() != key);
    EXPECT(display_dual_outputs(&out, 1) == 1 && out.live && out.role == CON_OUT_TEXT);
    adapter.gen++;
    EXPECT(display_dual_outputs(&out, 1) == 1 && !out.live);
    pm_console_text_state(1, 1, 1, adapter.gen);
    key = display_dual_key();
    pm_console_text_state(1, 1, 0, adapter.gen);
    EXPECT(display_dual_key() != key);
    EXPECT(display_dual_outputs(&out, 1) == 1 && !out.live);
    adapter.state = GFX2_AD_LOST;
    EXPECT(display_dual_outputs(&out, 1) == 1 && !out.live);
    adapter.state = GFX2_AD_OK;
    EXPECT(!DISPLAY_V1_PRESENT_OK());
    DISPLAY_V1_PUBLIC_INIT_OK();
    EXPECT(DISPLAY_V1_PRESENT_OK());
    adapter.state = GFX2_AD_LOST;
    EXPECT(!DISPLAY_V1_PRESENT_OK());
    adapter.state = GFX2_AD_OK;
    int initialized_by = current_app;
    current_app++;
    EXPECT(!DISPLAY_V1_PRESENT_OK());
    wm_actor = initialized_by;
    EXPECT(DISPLAY_V1_PRESENT_OK());
    adapter.gen++;
    EXPECT(!DISPLAY_V1_PRESENT_OK());
    wm_actor = 0;
    current_app--;
    EXPECT(!DISPLAY_V1_PRESENT_OK());
    DISPLAY_V1_PUBLIC_INIT_OK();
    EXPECT(DISPLAY_V1_PRESENT_OK());
    scroll_frames();
    display_provider_raster_reset();
    EXPECT(!DISPLAY_V1_PRESENT_OK());
    gfx_backend_of_fb.shutdown(); EXPECT(!of_fb_ready() && !sync.used);
    gfx_backend_of_fb.prepare(); EXPECT(of_fb_ready());
    gfx_backend_of_fb.init(); EXPECT(sync.used);
    pm_console_text_state(1, 1, 1, adapter.gen);
    EXPECT(!DISPLAY_V1_PRESENT_OK());
    pm_console_text_state(1, 1, 0, 0);
    EXPECT(!DISPLAY_V1_PRESENT_OK());
    EXPECT(display_provider_init_failed(&gfx_backend_of_fb) < 0 && !of_fb_ready());
}
/* OSRI contract, DESIGN.final §4. Observe native scanout at the wait boundary. */
#include "gfx/gfx_osri.h"
#include "gfx/gfx_osri_view.h"
static u8 osri_screen[648 * 402] __attribute__((aligned(4)));
static u8 osri_client[644 * 402], packed[GFX_WIDTH * GFX_HEIGHT];
static u8 planes[4][GFX_PLANE_SZ], clut[256][3];
static struct gfx_kernel_fb decoded;
static const struct of_fb_desc desc = {
    osri_screen, osri_client, 640, 402, 648, sizeof(osri_screen),
    644, sizeof(osri_client), 1, clut, palette_allowed
};
static unsigned long long bound;
static int expire_begin, expire_end, fail_display_bridge, restore_busy;
static int replace_bind, waits, bridges, keys, extend, sent, flip, abort_wait;
volatile u32 tick_count;
void *kmalloc(u32 n) { EXPECT(n == sizeof(packed)); return packed; }
void kfree(void *p) { EXPECT(p == packed); }
const struct gfx_kernel_fb *pm_osri_decoded(unsigned long long *value)
{ *value = bound; return &decoded; }
int pm_osri_key(void)
{
    if (waits == 1 && extend && !sent++) return 1;
    return waits >= 2 && (keys || extend) ? 0 : -1;
}
int gfx_osri_poll_abort(int caller) { EXPECT(caller == 3); return waits && abort_wait; }
int v1_bridge_begin(u32 role, u32 access)
{
    EXPECT(role == (bridges ? GFX2_ROLE_DISPLAY : GFX2_ROLE_CLIENT));
    EXPECT(access == (bridges ? GFX2_CPU_W : GFX2_CPU_R));
    if (bridges && waits && restore_busy) {
        restore_busy = 0;
        return OS32_ERR_BUSY;
    }
    if (bridges && fail_display_bridge) return OS32_ERR_BUSY;
    if (bridges && expire_begin) tick_count += 4;
    ++bridges;
    return 0;
}
void v1_bridge_end(u32 role, u32 access, u32 fence)
{
    if (bridges == 2 && expire_end) tick_count += 4;
    --bridges;
    EXPECT(role == (bridges ? GFX2_ROLE_DISPLAY : GFX2_ROLE_CLIENT));
    EXPECT(access == (bridges ? GFX2_CPU_W : GFX2_CPU_R) && !fence);
}
void idle_wait_step(void)
{
    EXPECT(!bridges);
    EXPECT(osri_screen[648] == 250 && osri_screen[649] == 251);
    EXPECT(osri_screen[650] == 252 && osri_screen[651] == 253);
    EXPECT(osri_screen[656] == 250 && osri_screen[647] == 17);
    EXPECT(osri_screen[1296] == (keys ? 250 : 254));
    osri_client[644] = 12;
    EXPECT(!of_fb_present(0, 1, 1, 1) && osri_screen[648] == 250);
    if (flip) {
        static u8 surface[644 * 402];
        surface[644] = 45;
        gfx2_surfs[1] = (struct gfx2_surf){ .sid = 1, .state = GFX2_ST_IDLE,
            .w = 640, .h = 402, .pitch = 644, .bytes = sizeof(surface),
            .usage = GFX2_USE_SCANOUT, .fmt = GFX2_FMT_INDEX8,
            .pool = GFX2_POOL_SYSMEM, .kva = surface };
        DISPLAY_V1_PUBLIC_INIT_OK();
        EXPECT(!gfx2_flip(0, 0, 1, 0, 0) && adapter.head[0].scan_sid == 1);
        EXPECT(osri_screen[648] == 250);
    }
    if (replace_bind) {
        EXPECT(!of_fb_bind(&desc));
        osri_screen[648] = 19;
    }
    ++waits;
    tick_count += 3;
}
static void osri(void)
{
    static struct OsriImage image;
    for (unsigned p = 0; p < 4; ++p) {
        decoded.planes[p] = planes[p];
        planes[p][0] = 128 >> p;
        image.pal[1 << p] = p + 1;
        clut[250 + p][2] = (p + 1) * 17;
    }
    planes[0][1] = planes[0][80] = 128;
    clut[254][0] = clut[255][0] = 255;
    image.event[0] = (struct OsriEvent){.line = 1, .index = 1, .rgb444 = 0xf00};
    for (int test = 0; test < 7; ++test) {
        kmemset(osri_screen, 17, sizeof(osri_screen));
        EXPECT(!of_fb_bind(&desc));
        /* The best physical indices are outside the logical mapping. */
        allow_palette = 1;
        const u8 black[3] = {0, 0, 0};
        EXPECT(!of_fb_palette(250, 1, black));
        osri_client[644 + 10] = 29;
        bound = of_fb_osri_generation();
        EXPECT(bound);
        waits = tick_count = 0;
        replace_bind = test == 2;
        keys = test == 1;
        extend = test == 3;
        flip = test == 4;
        abort_wait = test == 5;
        restore_busy = test == 6;
        sent = 0;
        image.count = !keys;
        struct OsriViewRun run = {3, 0, 20};
        int rc = gfx_backend_of_fb.osri_show(&image, &run);
        EXPECT(rc == (replace_bind ? OS32_ERR_IO : (keys || extend || abort_wait) ? GFX_OSRI_SHOW_CANCELLED : GFX_OSRI_SHOW_DONE));
        EXPECT(waits == (keys || extend || test == 6 ? 2 : 1) && !bridges);
        EXPECT(osri_screen[648] == (replace_bind ? 19 : flip ? 45 : 12));
        EXPECT(osri_screen[648 + 10] == (replace_bind || flip ? 0 : 29));
        of_fb_unbind();
    }
    /* No scanout if our frame has expired at acquisition. If expiration
     * happens during transfer, the committed source must be restored. */
    for (int test = 0; test < 3; ++test) {
        EXPECT(!of_fb_bind(&desc));
        bound = of_fb_osri_generation();
        expire_begin = test == 0;
        expire_end = test == 1;
        fail_display_bridge = test == 2;
        image.count = 1;
        tick_count = waits = bridges = 0;
        u32 before = gfx_counters.present_bytes;
        struct OsriViewRun run = {3, 0, 20};
        EXPECT(gfx_backend_of_fb.osri_show(&image, &run) ==
               (fail_display_bridge ? OS32_ERR_BUSY : OS32_ERR_AGAIN));
        EXPECT(!waits && !bridges && gfx_counters.present_bytes - before ==
               (expire_end ? 2u * GFX_WIDTH * GFX_HEIGHT : 0u));
        of_fb_unbind();
    }

}
void _start(void)
{
    copy_characterization(); raster(); provider(); osri();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failed) : "memory");
    __builtin_unreachable();
}
