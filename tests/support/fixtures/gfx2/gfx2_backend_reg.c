/* C1 dependency fakes; no other HAL v2 stage is linked. */
#include "sys/hal/gfx/v2/gfx2_int.h"
#include "module_runtime.h"
#include "redir_access.h"
#include "display_provider.h"
static int failed;
static void fail(const char *text, u32 len)
{
    failed = 1;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len) : "memory");
}
#define EXPECT(x) do { if (!(x)) fail("FAIL " #x "\n", sizeof("FAIL " #x "\n") - 1); } while (0)
static int heap_rc, init_rc, heap_calls, refreshes, lost_calls, recovered, detaches, rebinds;
static int owner_ok, owner_calls, warnings, stop_rc, stop_calls;
struct GfxSyncCtx { int mode; };
int gfxsync_mode(const GfxSyncCtx *c) { return c->mode; }
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner)
{
    EXPECT(kind == LEDGER_KIND_MODULE && id == 0 && name[0] == 'g');
    owner_calls++;
    if (owner_ok) *owner = 27;
    return owner_ok;
}
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; warnings++; }
int gfx2_heap_init(struct gfx2_heap *h, u32 base, u32 size)
{
    heap_calls++;
    if (size && init_rc) return init_rc;
    h->base = base; h->size = size; h->n = size ? 1 : 0;
    return 0;
}
void gfx2_caps_refresh(struct gfx2_adapter *a) { a->caps.heads = 3; refreshes++; }
void gfx2_head_detach(struct gfx2_adapter *a, u32 head)
{ detaches++; a->head[head].scan_sid = a->head[head].pend_sid = 0; }
int v1_bridge_rebind(void) { EXPECT(gfx2_active() != NULL); rebinds++; return 0; }
void gfx2_adapter_lost(struct gfx2_adapter *a, int held)
{ EXPECT(!held); lost_calls++; a->state = GFX2_AD_LOST; a->gen++; }
void gfx2_adapter_recovered(struct gfx2_adapter *a) { recovered++; a->state = GFX2_AD_OK; }
static int caller_ok, caller_calls;
int caller_access_get_user(struct caller_access *c)
{
    caller_calls++;
    if (!caller_ok) return 0;
    c->app_id = 42; c->as = (struct addrspace *)0x1234000; c->owner = 91;
    return 1;
}
static int heap_range(void *ctx, u32 *base, u32 *size, u32 *phys)
{
    (void)ctx;
    if (heap_rc) return heap_rc;
    *base = 0x1000; *size = 0x2000; *phys = 0xe0000000;
    return 0;
}
static int stop(void *ctx) { (void)ctx; stop_calls++; return stop_rc; }
static int fake_caps(void * p0, GFX2_Caps * p1)
{ (void)p0; (void)p1; return 0; }
static int fake_check(void * p0, const struct gfx2_exec * p1)
{ (void)p0; (void)p1; return 0; }
static int fake_alloc(void * p0, struct gfx2_surf * p1)
{ (void)p0; (void)p1; return 0; }
static int fake_release(void * p0, struct gfx2_surf * p1)
{ (void)p0; (void)p1; return 0; }
static int fake_kva(void * p0, const struct gfx2_surf * p1, u8 ** p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_gart_bind(void * p0, const u32 * p1, u32 p2, u32 * p3)
{ (void)p0; (void)p1; (void)p2; (void)p3; return 0; }
static int fake_gart_unbind(void * p0, u32 p1, u32 p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_emit(void * p0, const struct gfx2_exec * p1, u32 p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_readback(void * p0, const struct gfx2_exec * p1, u8 * p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_kick(void * p0)
{ (void)p0; return 0; }
static int fake_fence_emit(void * p0, u32 p1, u32 p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_fence_state(void * p0, u32 * p1, u32 * p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_fence_wait(void * p0, u32 p1, u32 p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_enum_head(void * p0, u32 p1, GFX2_HeadInfo * p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_enum_conn(void * p0, u32 p1, GFX2_ConnectorInfo * p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_enum_mode(void * p0, u32 p1, u32 p2, u32 p3, GFX2_ModeInfo * p4)
{ (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; return 0; }
static int fake_edid(void * p0, u32 p1, u32 p2, u8 * p3)
{ (void)p0; (void)p1; (void)p2; (void)p3; return 0; }
static int fake_mode_set(void * p0, u32 p1, u32 p2, const GFX2_ModeInfo * p3, const struct gfx2_surf * p4)
{ (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; return 0; }
static int fake_flip(void * p0, u32 p1, const struct gfx2_surf * p2, u32 p3, u32 p4)
{ (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; return 0; }
static int fake_flip_done(void * p0, u32 p1, u32 * p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_vblank_wait(void * p0, u32 p1, u32 p2, u32 p3, u32 * p4)
{ (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; return 0; }
static int fake_palette(void * p0, u32 p1, u32 p2, const u32 * p3)
{ (void)p0; (void)p1; (void)p2; (void)p3; return 0; }
static int fake_gamma(void * p0, u32 p1, const u16 * p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_cursor_image(void * p0, u32 p1, const struct gfx2_surf * p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_cursor_move(void * p0, u32 p1, i32 p2, i32 p3)
{ (void)p0; (void)p1; (void)p2; (void)p3; return 0; }
static int fake_cursor_show(void * p0, u32 p1, u32 p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
static int fake_dpms(void * p0, u32 p1, u32 p2)
{ (void)p0; (void)p1; (void)p2; return 0; }
#define OPS \
    .abi = GFX_HAL2_ABI, \
    .caps = fake_caps, \
    .check = fake_check, \
    .heap_range = heap_range, \
    .alloc = fake_alloc, \
    .release = fake_release, \
    .kva = fake_kva, \
    .gart_bind = fake_gart_bind, \
    .gart_unbind = fake_gart_unbind, \
    .emit = fake_emit, \
    .readback = fake_readback, \
    .kick = fake_kick, \
    .fence_emit = fake_fence_emit, \
    .fence_state = fake_fence_state, \
    .fence_wait = fake_fence_wait, \
    .enum_head = fake_enum_head, \
    .enum_conn = fake_enum_conn, \
    .enum_mode = fake_enum_mode, \
    .edid = fake_edid, \
    .mode_set = fake_mode_set, \
    .flip = fake_flip, \
    .flip_done = fake_flip_done, \
    .vblank_wait = fake_vblank_wait, \
    .palette = fake_palette, \
    .gamma = fake_gamma, \
    .cursor_image = fake_cursor_image, \
    .cursor_move = fake_cursor_move, \
    .cursor_show = fake_cursor_show, \
    .dpms = fake_dpms, \
    .stop = stop
const GfxBackendV2 gfx2_v1_shim = { OPS };
const GfxBackendV2 gfx2_lost_be = { OPS };
static GfxBackendV2 ops = { OPS };
static GfxBackend v1[6];

static int run(void)
{
    struct module_record mod = {.text = (u32)(uptr)fake_caps, .text_size = 1};
    v1[0].v2 = &ops;
    ops.abi++;
    if (gfx2_adapter_register(&v1[0], GFX2_KIND_GEFORCE, 0) != OS32_ERR_INVAL) return 1;
    ops.abi--;
    v1[0].v2_mod = &mod;
    if (gfx2_adapter_register(&v1[0], GFX2_KIND_GEFORCE, 0) != OS32_ERR_INVAL) return 2;
    v1[0].v2_mod = NULL;
    ops.cursor_move = NULL;
    if (gfx2_adapter_register(&v1[0], GFX2_KIND_GEFORCE, 0) != OS32_ERR_INVAL) return 7;
    ops.cursor_move = fake_cursor_move;
    if (gfx2_adapter_register(&v1[0], GFX2_KIND_GEFORCE, GFX2_AF_ACTIVE) ||
        !gfx2_active() || gfx2_quar_owner || warnings != 1) return 3;
    owner_ok = 1;
    if (gfx2_adapter_register(&v1[0], GFX2_KIND_GEFORCE, GFX2_AF_ACTIVE) ||
        owner_calls != 2 || !gfx2_quar_owner) return 3;
    if (gfx2_adapter_register(&v1[1], GFX2_KIND_PC98, 0) ||
        gfx2_adapter_get(1)->be != &gfx2_v1_shim) return 4;
    GfxSyncCtx direct = {.mode = GFXSYNC_DIRECT}, *ctx;
    u32 mode;
    if (gfx2_sync_publish(GFX2_KIND_GEFORCE, 0, &direct) ||
        gfx2_sync_get(gfx2_active(), 0, &ctx, &mode) || mode != GFXSYNC_DIRECT) return 5;
    direct.mode = GFXSYNC_SHADOW;
    if (gfx2_sync_get(gfx2_active(), 0, &ctx, &mode) || mode != GFXSYNC_SHADOW ||
        gfx2_sync_publish(GFX2_KIND_GEFORCE, 0, NULL) ||
        gfx2_sync_get(gfx2_active(), 0, &ctx, &mode) != OS32_ERR_NOTFOUND) return 5;
    return failed ? 6 : 0;
}
void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(run()));
    __builtin_unreachable();
}
