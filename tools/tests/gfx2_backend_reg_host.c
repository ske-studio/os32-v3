/* C1 dependency fakes; no other HAL v2 stage is linked. */
#include "gfx/v2/gfx2_int.h"
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
void v1_bridge_rebind(void) { EXPECT(gfx2_active() != NULL); rebinds++; }
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
static int probe_calls[5];
static int probe0(void) { probe_calls[0]++; return 1; }
static int probe1(void) { probe_calls[1]++; return 1; }
static int probe2(void) { probe_calls[2]++; return 1; }
static int probe3(void) { probe_calls[3]++; return 1; }
static int probe4(void) { probe_calls[4]++; return 1; }
static const GfxMode modes[1];
GfxBackend gfx_backend_trident = { .modes = modes, .probe = probe0 };
GfxBackend gfx_backend_geforce = { .modes = modes, .probe = probe1 };
GfxBackend gfx_backend_cirrus = { .modes = modes, .probe = probe2 };
GfxBackend gfx_backend_pegc = { .modes = modes, .probe = probe3 };
GfxBackend gfx_backend_pc98 = { .modes = modes, .probe = probe4 };
GfxBackend *g_backend = &gfx_backend_geforce;
static u32 enumerated[5], enum_count;
static void record_backend(GfxBackend *be, void *arg)
{
    EXPECT(arg == &ops && enum_count < 5);
    enumerated[enum_count++] = display_provider_ledger(be);
}
static void test_provider(void)
{
    display_provider_each(record_backend, &ops);
    EXPECT(enum_count == 4 && enumerated[0] == GFX2_KIND_GEFORCE &&
           enumerated[1] == GFX2_KIND_CIRRUS && enumerated[2] == GFX2_KIND_PEGC &&
           enumerated[3] == GFX2_KIND_PC98);
    EXPECT(!probe_calls[0] && !probe_calls[1] && probe_calls[2] == 1 &&
           probe_calls[3] == 1 && probe_calls[4] == 1 && !gfx_cirrus_probed());
    enum_count = 0; g_backend = &gfx_backend_trident;
    display_provider_each(record_backend, &ops);
    EXPECT(enum_count == 4 && enumerated[0] == GFX2_KIND_TRIDENT && !probe_calls[0]);
    enum_count = 0; gfx_backend_cirrus.probe = NULL;
    gfx_backend_pegc.modes = NULL; g_backend = &gfx_backend_pc98;
    display_provider_each(record_backend, &ops);
    EXPECT(enum_count == 1 && enumerated[0] == GFX2_KIND_PC98);
}
static void test_registry(void)
{
    struct gfx2_adapter *a, *b;
    struct module_record mod = { .text = 0x08000000, .text_size = 0x01000000 };
    EXPECT(!gfx2_adapter_get(0) && !gfx2_adapter_get(GFX2_MAX_ADAPTERS) && !gfx2_active());
    v1[0].v2 = &ops;
    ops.abi++;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    EXPECT(!gfx2_adapter_get(0) && owner_calls == 0);
    ops.abi--;
    ops.caps = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.caps = fake_caps;
    ops.check = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.check = fake_check;
    ops.heap_range = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.heap_range = heap_range;
    ops.alloc = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.alloc = fake_alloc;
    ops.release = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.release = fake_release;
    ops.kva = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.kva = fake_kva;
    ops.gart_bind = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.gart_bind = fake_gart_bind;
    ops.gart_unbind = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.gart_unbind = fake_gart_unbind;
    ops.emit = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.emit = fake_emit;
    ops.readback = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.readback = fake_readback;
    ops.kick = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.kick = fake_kick;
    ops.fence_emit = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.fence_emit = fake_fence_emit;
    ops.fence_state = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.fence_state = fake_fence_state;
    ops.fence_wait = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.fence_wait = fake_fence_wait;
    ops.enum_head = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.enum_head = fake_enum_head;
    ops.enum_conn = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.enum_conn = fake_enum_conn;
    ops.enum_mode = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.enum_mode = fake_enum_mode;
    ops.edid = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.edid = fake_edid;
    ops.mode_set = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.mode_set = fake_mode_set;
    ops.flip = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.flip = fake_flip;
    ops.flip_done = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.flip_done = fake_flip_done;
    ops.vblank_wait = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.vblank_wait = fake_vblank_wait;
    ops.palette = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.palette = fake_palette;
    ops.gamma = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.gamma = fake_gamma;
    ops.cursor_image = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.cursor_image = fake_cursor_image;
    ops.cursor_move = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.cursor_move = fake_cursor_move;
    ops.cursor_show = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.cursor_show = fake_cursor_show;
    ops.dpms = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.dpms = fake_dpms;
    ops.stop = NULL;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.stop = stop;
    v1[0].v2_mod = &mod;
    /* Every real callback is in this broad fake text interval. */
    EXPECT(gfx2_adapter_register(&v1[0], 4, GFX2_AF_ACTIVE) == 0);
    a = gfx2_adapter_get(0);
    EXPECT(a && a->index == 0 && a->gen == 1 && a->kind == 4 && a->be == &ops && a->heads == 2);
    EXPECT(gfx2_active() == a && !gfx2_quar_owner && owner_calls == 1 && warnings == 1);
    EXPECT(a->heap.base == 0x1000 && a->heap.size == 0x2000 && a->vram_phys == 0xe0000000);
    a->heads = 2; a->head[0].scan_sid = 19; a->head[1].pend_sid = 20;
    a->heap.n = 7;
    owner_ok = 1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == 0);
    EXPECT(gfx2_adapter_get(0) == a && !gfx2_adapter_get(1) && a->gen == 1 &&
           a->heap.n == 7 && heap_calls == 1 && refreshes == 1 &&
           a->head[0].scan_sid == 19 && gfx2_quar_owner == 27 && owner_calls == 2);
    /* Module bounds are tested separately for each hook below. */
    ops.caps = (__typeof__(ops.caps))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.caps = fake_caps;
    ops.check = (__typeof__(ops.check))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.check = fake_check;
    ops.heap_range = (__typeof__(ops.heap_range))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.heap_range = heap_range;
    ops.alloc = (__typeof__(ops.alloc))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.alloc = fake_alloc;
    ops.release = (__typeof__(ops.release))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.release = fake_release;
    ops.kva = (__typeof__(ops.kva))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.kva = fake_kva;
    ops.gart_bind = (__typeof__(ops.gart_bind))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.gart_bind = fake_gart_bind;
    ops.gart_unbind = (__typeof__(ops.gart_unbind))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.gart_unbind = fake_gart_unbind;
    ops.emit = (__typeof__(ops.emit))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.emit = fake_emit;
    ops.readback = (__typeof__(ops.readback))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.readback = fake_readback;
    ops.kick = (__typeof__(ops.kick))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.kick = fake_kick;
    ops.fence_emit = (__typeof__(ops.fence_emit))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.fence_emit = fake_fence_emit;
    ops.fence_state = (__typeof__(ops.fence_state))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.fence_state = fake_fence_state;
    ops.fence_wait = (__typeof__(ops.fence_wait))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.fence_wait = fake_fence_wait;
    ops.enum_head = (__typeof__(ops.enum_head))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.enum_head = fake_enum_head;
    ops.enum_conn = (__typeof__(ops.enum_conn))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.enum_conn = fake_enum_conn;
    ops.enum_mode = (__typeof__(ops.enum_mode))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.enum_mode = fake_enum_mode;
    ops.edid = (__typeof__(ops.edid))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.edid = fake_edid;
    ops.mode_set = (__typeof__(ops.mode_set))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.mode_set = fake_mode_set;
    ops.flip = (__typeof__(ops.flip))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.flip = fake_flip;
    ops.flip_done = (__typeof__(ops.flip_done))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.flip_done = fake_flip_done;
    ops.vblank_wait = (__typeof__(ops.vblank_wait))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.vblank_wait = fake_vblank_wait;
    ops.palette = (__typeof__(ops.palette))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.palette = fake_palette;
    ops.gamma = (__typeof__(ops.gamma))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.gamma = fake_gamma;
    ops.cursor_image = (__typeof__(ops.cursor_image))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.cursor_image = fake_cursor_image;
    ops.cursor_move = (__typeof__(ops.cursor_move))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.cursor_move = fake_cursor_move;
    ops.cursor_show = (__typeof__(ops.cursor_show))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.cursor_show = fake_cursor_show;
    ops.dpms = (__typeof__(ops.dpms))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.dpms = fake_dpms;
    ops.stop = (__typeof__(ops.stop))1;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL);
    ops.stop = stop;
    EXPECT(a->gen == 1 && rebinds == 2 && owner_calls == 2);
    v1[0].v2_mod = NULL;
    GfxBackendV2 replacement = ops;
    v1[0].v2 = &replacement;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == OS32_ERR_INVAL && a->be == &ops);
    v1[0].v2 = &ops;
    EXPECT(gfx2_adapter_register(&v1[1], 1, 1) == 0);
    b = gfx2_adapter_get(1);
    EXPECT(b && b->be == &gfx2_v1_shim && b->ctx == b && gfx2_active() == b);
    EXPECT(!(a->flags & GFX2_AF_ACTIVE) && a->gen == 2 && detaches == 2 &&
           !a->head[0].scan_sid && !a->head[1].pend_sid);
    EXPECT(gfx2_adapter_register(&v1[1], 1, 1) == 0 && b->gen == 1 && detaches == 2);
    a->gen = 0xffffffffU;
    gfx2_adapter_activate(a); gfx2_adapter_activate(b);
    EXPECT(a->gen == 1);
    gfx2_adapter_unregister(&v1[0]);
    EXPECT(a->be == &gfx2_lost_be && a->ctx == a && a->state == GFX2_AD_LOST && lost_calls == 1);
    gfx2_adapter_unregister(&v1[0]);
    EXPECT(lost_calls == 1);
    v1[0].v2 = &replacement;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == 0 && a->be == &replacement &&
           a->state == GFX2_AD_OK && recovered == 1);
    a->state = GFX2_AD_LOST_HELD; stop_rc = OS32_ERR_IO;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == 0 && recovered == 1 && stop_calls == 1);
    stop_rc = 0;
    EXPECT(gfx2_adapter_register(&v1[0], 4, 1) == 0 && recovered == 2 && stop_calls == 2);
    heap_rc = OS32_ERR_NOSYS;
    EXPECT(gfx2_adapter_register(&v1[2], 2, 0) == 0);
    struct gfx2_adapter *c = gfx2_adapter_get(2);
    EXPECT(c->heap.size == 0 && c->vram_phys == 0 && warnings == 2);
    heap_rc = 0; init_rc = OS32_ERR_INVAL;
    EXPECT(gfx2_adapter_register(&v1[2], 2, 0) == 0 && !c->heap.size && !c->vram_phys);
    init_rc = 0;
    EXPECT(gfx2_adapter_register(&v1[2], 2, 0) == 0 && c->heap.size == 0x2000);
    EXPECT(gfx2_adapter_register(&v1[3], 3, GFX2_AF_ONBOARD) == 0);
    EXPECT(gfx2_adapter_get(3)->flags == GFX2_AF_ONBOARD);
    EXPECT(gfx2_adapter_register(&v1[4], 5, 1) == OS32_ERR_FULL && gfx2_active() == a);
    EXPECT(gfx2_adapter_register(NULL, 1, 0) == OS32_ERR_INVAL);
    EXPECT(gfx2_adapter_register(&v1[5], 0, 0) == OS32_ERR_INVAL);
    EXPECT(gfx2_adapter_register(&v1[5], 6, 0) == OS32_ERR_INVAL);
    EXPECT(gfx2_adapter_register(&v1[5], 1, 4) == OS32_ERR_INVAL);
}
static void test_sync_caller(void)
{
    struct gfx2_adapter *a = gfx2_adapter_get(0);
    struct GfxSyncCtx direct = { GFXSYNC_DIRECT }, shadow = { GFXSYNC_SHADOW };
    GfxSyncCtx *ctx = &direct; u32 mode = 99;
    EXPECT(gfx2_sync_get(a, 0, &ctx, &mode) == OS32_ERR_NOTFOUND && !ctx);
    EXPECT(!gfx2_sync_publish(a->kind, 0, &direct));
    EXPECT(!gfx2_sync_get(a, 0, &ctx, &mode) && ctx == &direct && mode == GFXSYNC_DIRECT);
    direct.mode = GFXSYNC_SHADOW;
    EXPECT(!gfx2_sync_get(a, 0, &ctx, &mode) && mode == GFXSYNC_SHADOW);
    EXPECT(!gfx2_sync_publish(a->kind, 0, &shadow));
    EXPECT(!gfx2_sync_get(a, 0, &ctx, &mode) && ctx == &shadow && mode == GFXSYNC_SHADOW);
    EXPECT(gfx2_sync_publish(0, 0, NULL) == OS32_ERR_INVAL);
    EXPECT(gfx2_sync_publish(GFX2_KIND_MAX + 1, 0, NULL) == OS32_ERR_INVAL);
    EXPECT(gfx2_sync_publish(a->kind, 2, NULL) == OS32_ERR_INVAL);
    EXPECT(!gfx2_sync_get(a, 0, &ctx, &mode) && ctx == &shadow);
    EXPECT(!gfx2_sync_publish(a->kind, 0, NULL));
    EXPECT(gfx2_sync_get(a, 0, &ctx, &mode) == OS32_ERR_NOTFOUND && !ctx);
    EXPECT(gfx2_sync_get(a, 2, &ctx, &mode) == OS32_ERR_NOTFOUND && !ctx);
    struct gfx2_caller caller = { 3, NULL, 5 };
    EXPECT(gfx2_caller_get(&caller) == OS32_ERR_INVAL && caller.app_id == 3 && caller.owner == 5);
    caller_ok = 1;
    EXPECT(!gfx2_caller_get(&caller) && caller.app_id == 42 && caller.owner == 91 &&
           caller.as == (struct addrspace *)0x1234000 && caller_calls == 2);
}
/* Execute the actual gfx_core registration helpers, init and shutdown callers. */
#include "gfx/gfx.h"
static int gfx_started, gfx_current_height, gfx_flip_enabled, gfx_display_page;
static u32 gfx_init_gen;
static u8 *bb[4];
static struct { u32 count; } prev_dirty;
static int init_result = 1, palette_resets, core_reinits;
static GfxBackend *select_backend;
static int ring3_call_from_user(void) { return 0; }
static int con_sink_is_enabled(void) { return 0; }
static int appslot_gfx_owner(void) { return 42; }
static int pc98_route_safe(void) { return 1; }
static void pc98_splash_raster_reset(void) { }
static void gfx_policy_reeval(void) { }
static void gfx_reinit_surfaces(int finish) { (void)finish; core_reinits++; }
static int gfx_select_mode(GfxBackend *be, int legacy) { (void)be; (void)legacy; return 1; }
static void gfx_bind_client(void) { }
static void gfx_diag_init(void) { }
static void palette_shadow_reset(void) { }
void gfx2_pal_reset16(void) { palette_resets++; }
static int gfx_select_and_init_backend(int legacy)
{ (void)legacy; g_backend = select_backend; return init_result; }
static void _gfx_common_init(int size) { (void)size; }
static void display_planar_start(int legacy) { (void)legacy; }
static void gfx_clear_planar_pages(int size) { (void)size; }
static void display_flip_pages(int d, int a) { (void)d; (void)a; }
static void palette_init(void) { }
static void gfx_scroll_init(void) { }
#include "gfx2_core_calls.inc"
static void test_core_calls(void)
{
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++)
        gfx2_adapters[i] = (struct gfx2_adapter){0};
    gfx_backend_pegc.modes = modes; gfx_backend_cirrus.probe = probe2;
    select_backend = &gfx_backend_geforce;
    int binds = rebinds;
    gfx_init_mode(0);
    struct gfx2_adapter *a = gfx2_active();
    EXPECT(a && a->v1 == select_backend && palette_resets == 1 && rebinds == binds + 1);
    EXPECT(gfx2_adapter_get(3) && !gfx2_adapter_get(4));
    if (!a) return;
    a->heads = 2; a->head[0].scan_sid = 32;
    u32 gen = a->gen;
    gfx_init_mode(1);
    EXPECT(gfx2_active() == a && a->gen == gen && a->head[0].scan_sid == 32);
    gfx_shutdown();
    EXPECT(gfx2_active()->v1 == &gfx_backend_pc98 && a->gen == gen + 1 && !a->head[0].scan_sid);
    struct gfx2_adapter *b = gfx2_active();
    b->heads = 1; b->head[0].scan_sid = 55;
    gfx_shutdown();
    EXPECT(!b->head[0].scan_sid && gfx2_active() == b);
    select_backend = &gfx_backend_pc98; init_result = 0;
    gfx_init_mode(0);
    EXPECT(gfx2_active() == b && palette_resets == 3);
    init_result = -1; select_backend = &gfx_backend_geforce;
    gfx_init_mode(0);
    EXPECT(gfx2_active() == b && palette_resets == 4);
    for (u32 heads = 0; heads <= 3; heads++) {
        b->caps.heads = heads;
        EXPECT(!gfx2_adapter_register(b->v1, b->kind, 0));
        EXPECT(b->heads == (heads < 2 ? heads : 2));
    }
}
void _start(void)
{
    test_provider(); test_registry(); test_sync_caller(); test_core_calls();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failed) : "memory");
    __builtin_unreachable();
}
