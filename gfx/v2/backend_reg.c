/* HAL v2 adapter registry (HAL_V2_DESIGN §11-7, C1). */
#include "gfx2_int.h"
#include "module_runtime.h"
#include "redir_access.h"
#include "kprintf.h"

struct gfx2_adapter gfx2_adapters[GFX2_MAX_ADAPTERS];
u32 gfx2_quar_owner;
static GfxSyncCtx *sync_tab[GFX2_KIND_MAX + 1][2];
static u8 heap_retry[GFX2_MAX_ADAPTERS];
static int quar_warned, heap_warned;

static struct gfx2_adapter *adapter_by_v1(GfxBackend *v1)
{
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++)
        if (gfx2_adapters[i].v1 == v1) return &gfx2_adapters[i];
    return NULL;
}

struct gfx2_adapter *gfx2_adapter_get(u32 index)
{
    if (index >= GFX2_MAX_ADAPTERS || !gfx2_adapters[index].v1) return NULL;
    return &gfx2_adapters[index];
}

int gfx2_adapter_gen(u32 index, u32 *gen)
{
    struct gfx2_adapter *a = gfx2_adapter_get(index);
    if (!a || !gen) return OS32_ERR_INVAL;
    *gen = a->gen;
    return 0;
}

struct gfx2_adapter *gfx2_active(void)
{
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++)
        if (gfx2_adapters[i].flags & GFX2_AF_ACTIVE) return &gfx2_adapters[i];
    return NULL;
}

static int be_check(const GfxBackendV2 *be, const struct module_record *mod)
{
    if (be->abi != GFX_HAL2_ABI) return OS32_ERR_INVAL;
#define CHECK_HOOK(hook) do { \
    if (!be->hook) return OS32_ERR_INVAL; \
    if (mod && !module_text_contains(mod, (u32)(uptr)be->hook)) return OS32_ERR_INVAL; \
} while (0)
    CHECK_HOOK(caps);
    CHECK_HOOK(check);
    CHECK_HOOK(heap_range);
    CHECK_HOOK(alloc);
    CHECK_HOOK(release);
    CHECK_HOOK(kva);
    CHECK_HOOK(gart_bind);
    CHECK_HOOK(gart_unbind);
    CHECK_HOOK(emit);
    CHECK_HOOK(readback);
    CHECK_HOOK(kick);
    CHECK_HOOK(fence_emit);
    CHECK_HOOK(fence_state);
    CHECK_HOOK(fence_wait);
    CHECK_HOOK(enum_head);
    CHECK_HOOK(enum_conn);
    CHECK_HOOK(enum_mode);
    CHECK_HOOK(edid);
    CHECK_HOOK(mode_set);
    CHECK_HOOK(flip);
    CHECK_HOOK(flip_done);
    CHECK_HOOK(vblank_wait);
    CHECK_HOOK(palette);
    CHECK_HOOK(gamma);
    CHECK_HOOK(cursor_image);
    CHECK_HOOK(cursor_move);
    CHECK_HOOK(cursor_show);
    CHECK_HOOK(dpms);
    CHECK_HOOK(stop);
#undef CHECK_HOOK
    return 0;
}

/* Retry only an empty heap left by failure; repeated init preserves allocations. */
static void adapter_heap_init(struct gfx2_adapter *a)
{
    u32 base = 0, size = 0, phys = 0;
    int rc = a->be->heap_range(a->ctx, &base, &size, &phys);
    if (!rc) rc = gfx2_heap_init(&a->heap, base, size);
    heap_retry[a->index] = rc != 0;
    if (rc) {
        phys = 0;
        (void)gfx2_heap_init(&a->heap, 0, 0);
        if (!heap_warned) {
            heap_warned = 1;
            kprintf(KLOG_ATTR, "[gfx2] heap unavailable; using empty heap\n");
        }
    }
    a->vram_phys = phys;
}

int gfx2_adapter_register(GfxBackend *v1, u32 kind, u32 flags)
{
    struct gfx2_adapter *a;
    const GfxBackendV2 *be;
    int rebound;
    if (!v1 || !kind || kind > GFX2_KIND_MAX ||
        (flags & ~(GFX2_AF_ACTIVE | GFX2_AF_ONBOARD))) return OS32_ERR_INVAL;
    be = v1->v2 ? v1->v2 : &gfx2_v1_shim;
    if (v1->v2 && be_check(be, v1->v2_mod)) return OS32_ERR_INVAL;
    a = adapter_by_v1(v1);
    if (a && (a->kind != kind || (a->be != be && a->be != &gfx2_lost_be)))
        return OS32_ERR_INVAL;
    if (!a) {
        for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++) {
            if (!gfx2_adapters[i].v1) { a = &gfx2_adapters[i]; break; }
        }
        if (!a) return OS32_ERR_FULL;
    }
    /* All rejecting checks precede the first shared-state change. */
    if (!gfx2_quar_owner &&
        !ledger_owner_new(LEDGER_KIND_MODULE, 0, "gfx2-quar", &gfx2_quar_owner) &&
        !quar_warned) {
        quar_warned = 1;
        kprintf(KLOG_ATTR, "[gfx2] quarantine owner unavailable\n");
    }
    if (!a->v1) {
        a->index = (u32)(a - gfx2_adapters);
        a->gen = 1;
        a->v1 = v1;
        a->kind = kind;
        a->flags = flags & GFX2_AF_ONBOARD;
    }
    rebound = a->be != be;
    if (rebound) {
        a->be = be;
        a->ctx = v1->v2 ? be->ctx : a;
        heap_retry[a->index] = 1;
    }
    if (heap_retry[a->index]) adapter_heap_init(a);
    if (rebound) gfx2_caps_refresh(a);
    a->heads = a->caps.heads < 2 ? a->caps.heads : 2;
    if (flags & GFX2_AF_ACTIVE) {
        if (a->state != GFX2_AD_OK &&
            (a->state != GFX2_AD_LOST_HELD || !a->be->stop(a->ctx)))
            gfx2_adapter_recovered(a);
        gfx2_adapter_activate(a);
    }
    return 0;
}

void gfx2_adapter_activate(struct gfx2_adapter *a)
{
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++) {
        struct gfx2_adapter *o = &gfx2_adapters[i];
        if (o == a || !(o->flags & GFX2_AF_ACTIVE)) continue;
        o->flags &= ~GFX2_AF_ACTIVE;
        for (u32 h = 0; h < o->heads; h++) gfx2_head_detach(o, h);
        if (++o->gen == 0) o->gen = 1;
    }
    a->flags |= GFX2_AF_ACTIVE;
    v1_bridge_rebind();
}

void gfx2_adapter_unregister(GfxBackend *v1)
{
    struct gfx2_adapter *a = v1 ? adapter_by_v1(v1) : NULL;
    if (!a || a->be == &gfx2_lost_be) return;
    gfx2_adapter_lost(a, 0);
    a->be = &gfx2_lost_be;
    a->ctx = a;
}

int gfx2_caller_get(struct gfx2_caller *c)
{
    struct caller_access caller;
    if (!c || !caller_access_get_user(&caller)) return OS32_ERR_INVAL;
    c->app_id = caller.app_id;
    c->as = caller.as;
    c->owner = caller.owner;
    return 0;
}

int gfx2_sync_publish(u32 kind, u32 head, GfxSyncCtx *ctx)
{
    if (!kind || kind > GFX2_KIND_MAX || head >= 2) return OS32_ERR_INVAL;
    sync_tab[kind][head] = ctx;
    return 0;
}

int gfx2_sync_get(const struct gfx2_adapter *a, u32 head, GfxSyncCtx **ctx, u32 *mode)
{
    *ctx = NULL;
    *mode = 0;
    if (!a || !a->kind || a->kind > GFX2_KIND_MAX || head >= 2 ||
        !sync_tab[a->kind][head]) return OS32_ERR_NOTFOUND;
    *ctx = sync_tab[a->kind][head];
    *mode = (u32)gfxsync_mode(*ctx);
    return 0;
}
