/* Run the real probe through its public entry with a failing DMA adapter.
 * A second call must be rejected before borrowing the window or touching RAM. */
#include "sys/platform/pc98/kdebug/gfx_transfer_bench.c"

static u8 ram_pool[RAM_PAGES * PAGE_SIZE] __attribute__((aligned(4096)));
static u8 test_vram[GF_MODE_VRAM_BYTES];
static int maps, closes, uploads, downloads, allocations, frees, messages;
static u32 ticks;
static bool fail_upload, corrupt_guard;
static int clock_us(void *owner, u32 *value) { *value = ticks++; return 0; }
static int point(void *owner) { return 0; }
static int read_io(void *owner, u32 bar, u32 off, u32 width, u32 *value)
{ *value = 0; return 0; }
static u32 bar0[GF_PLAN_PFB_AMOUNT / 4 + 1];
static struct gf_foundation foundation = {
    .bar0 = (volatile u8 *)bar0,
    .io = {.read = read_io, .point = point, .timing.clock_us = clock_us}
};
static struct gf2_chan channel = {.f = &foundation};
static struct gf3_ctx context = {.ready = true, .ch = &channel};
static const struct gf3_surface surface = {
    .fmt = GF3_INDEX8, .off = GF_MODE_VRAM_BYTES, .bytes = sizeof(test_vram),
    .width = GF_MODE_WIDTH, .height = GF_MODE_HEIGHT, .pitch = GF_MODE_PITCH
};
static int map(struct gf_vram_view *v, struct gf2_chan *c, u32 off, u32 len,
               volatile u8 **out)
{
    if (off != surface.off || len > surface.bytes) return GF3_ERANGE;
    maps++; v->mapped = true; *out = test_vram; return 0;
}
static int unmap(struct gf_vram_view *v) { closes++; v->mapped = false; return 0; }
static int download(struct gf3_ctx *c, const struct gf3_surface *s,
                    struct gf3_rect r, struct gf3_ram *ram, u32 dl)
{
    downloads++;
    for (u32 y = 0; y < r.h; y++)
        for (u32 x = 0; x < r.w; x++)
            ((u8 *)ram->ptr)[y * ram->pitch + x] = test_vram[(r.y + y) * s->pitch + r.x + x];
    if (corrupt_guard) ((u8 *)ram->ptr)[r.h * ram->pitch]++;
    channel.seq++;
    return 0;
}
static int upload(struct gf3_ctx *c, const struct gf3_surface *s,
                  struct gf3_rect r, const struct gf3_ram *ram, bool forced, u32 dl)
{
    if (forced) uploads++;
    if (fail_upload) return GF2_EIO;
    for (u32 y = 0; y < r.h; y++)
        for (u32 x = 0; x < r.w; x++)
            test_vram[(r.y + y) * s->pitch + r.x + x] = ((u8 *)ram->ptr)[y * ram->pitch + x];
    channel.seq++;
    return 0;
}
const struct gf3_test_ops *geforce_accel_ops(void)
{
    static const struct gf3_test_ops ops = {
        .ctx = &context, .test_surface = &surface, .map = map, .unmap = unmap,
        .upload = upload, .download = download
    };
    return &ops;
}
u32 recover_gf_dev(void) { return 12; }
int gfx_surface_source(u32 role, struct surface_query_source *out)
{
    out->backend = LEDGER_SF_GEFORCE; out->role = role; out->count = 1;
    return 0;
}
struct ledger_surface *ledger_surface_find(u32 backend, u32 role)
{
    static struct ledger_surface scan = {.pitch = GF_MODE_PITCH, .format = GFX_FMT_PACKED8};
    return &scan;
}
void gfx_screen_info(void *ptr)
{
    GFX_ScreenInfo *s = ptr;
    s->flags = GFX_CAP_DIRECT; s->format = GFX_FMT_PACKED8;
}
int arch_video_wc_active(void) { return 1; }
int serial_gate_active(void) { return 0; }
void serial_puts_polled(const char *s) { if (s[0] == 'G' && s[2] == 'm' && s[3] == ':') messages++; }
u32 pgalloc_alloc_phys(u32 owner, int n)
{ allocations++; return n == RAM_PAGES ? (u32)(uptr)ram_pool : 0; }
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n) { frees++; return 1; }
void _start(void)
{
    struct trial t = {.face = surface, .rect = {4, 0, 17, 5}, .ram = {ram_pool + 7, 120, 24},
        .io = &foundation.io, .channel = &channel, .vram = test_vram + 4,
        .storage = ram_pool, .offset = 3};
    int rc = 0;
    for (int get = 0; get <= 1; get++) {
        t.get = get;
        if (group(&t, 1, false) || group(&t, 1, true)) rc = 3;
    }
    corrupt_guard = true;
    if (!group(&t, 2, true)) rc = 3;
    corrupt_guard = false;
    uploads = downloads = 0;
    fail_upload = true;
    if (!rc) rc = gfx_transfer_bench() != GF2_EIO || uploads != 1 || downloads != 1 ||
             closes != 1 || frees != 1 || messages != 1;
    if (!rc && (gfx_transfer_bench() != GF3_ESTATE || maps != 1 ||
                allocations != 1 || messages != 1)) rc = 2;
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc));
    __builtin_unreachable();
}
