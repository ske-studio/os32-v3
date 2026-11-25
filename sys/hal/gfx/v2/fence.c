/* HAL v2 fence and CPU synchronization. */
#include "gfx2_int.h"
#include "arch_cpu.h"
#include "gfx_policy.h"

#define GFX2_EMIT_DL 1000000u
#define GFX2_SWEEP_STEP 0x40000000UL

static int reached(u32 completed, u32 f)
{
    return !f || (i32)(completed - f) >= 0;
}

static void sweep(struct gfx2_adapter *a)
{
    u32 i;
    for (i = 0; i < GFX2_MAX_SURFACES; i++) {
        struct gfx2_surf *s = &gfx2_surfs[i];
        if (s->state == GFX2_ST_FREE || s->adapter != a->index) continue;
        if (reached(a->fence.completed, s->last_write)) s->last_write = 0;
        if (reached(a->fence.completed, s->last_read)) s->last_read = 0;
    }
}

void gfx2_adapter_lost(struct gfx2_adapter *a, int held)
{
    u32 i;
    if (a->state != GFX2_AD_OK) {
        if (held) a->state = GFX2_AD_LOST_HELD;
        return;
    }
    a->state = GFX2_AD_LOST;
    a->gen++;
    for (i = 0; i < GFX2_MAX_SURFACES; i++) {
        struct gfx2_surf *s = &gfx2_surfs[i];
        if (s->state != GFX2_ST_FREE && s->adapter == a->index && s->last_write &&
            !reached(a->fence.completed, s->last_write) &&
            reached(a->fence.failed_upto, s->last_write))
            (void)gfx2_surf_state_set(s, GFX2_ST_LOST);
    }
    if (held || a->be->stop(a->ctx)) a->state = GFX2_AD_LOST_HELD;
    gfx_policy_reeval();
}

void gfx2_adapter_recovered(struct gfx2_adapter *a)
{
    a->state = GFX2_AD_OK;
    gfx2_quarantine_drain(a);
}

static int refresh(struct gfx2_adapter *a)
{
    u32 completed, failed;
    int rc;
    if (a->state != GFX2_AD_OK) return OS32_ERR_STALE;
    rc = a->be->fence_state(a->ctx, &completed, &failed);
    if (rc) { a->fence.failed_upto = a->fence.issued; gfx2_adapter_lost(a, 0); return rc; }
    if (failed && failed != a->fence.failed_upto) {
        a->fence.failed_upto = failed;
        gfx2_adapter_lost(a, 0);
        return 0;
    }
    a->fence.completed = completed;
    return 0;
}

u32 gfx2_fence_reserve(struct gfx2_adapter *a)
{
    u32 f;
    if (a->state != GFX2_AD_OK) return 0;
    if (a->fence.reserved) return a->fence.reserved;
    f = a->fence.issued;
    if (a->fence.failed_upto && (i32)(a->fence.failed_upto - f) > 0)
        f = a->fence.failed_upto;
    f++;
    if (!f) f = 1;
    a->fence.reserved = f;
    return f;
}

static void note(struct gfx2_surf *dst, struct gfx2_surf *src, u32 f)
{
    if (dst) dst->last_write = f;
    if (src) src->last_read = f;
}

void gfx2_fence_commit_fail(struct gfx2_adapter *a, u32 f, struct gfx2_surf *dst, struct gfx2_surf *src, int rc)
{
    (void)rc;
    note(dst, src, f);
    a->fence.reserved = 0;
    a->fence.failed_upto = f;
    gfx2_adapter_lost(a, 0);
}

int gfx2_fence_commit(struct gfx2_adapter *a, u32 f, struct gfx2_surf *dst, struct gfx2_surf *src)
{
    int rc = a->be->fence_emit(a->ctx, f, GFX2_EMIT_DL);
    note(dst, src, f);
    a->fence.reserved = 0;
    if (rc) { gfx2_fence_commit_fail(a, f, NULL, NULL, rc); return rc; }
    a->fence.issued = f;
    if ((f / GFX2_SWEEP_STEP) != (a->fence.last_sweep / GFX2_SWEEP_STEP)) {
        (void)refresh(a);
        sweep(a);
        a->fence.last_sweep = f;
    }
    return 0;
}

void gfx2_fence_abandon(struct gfx2_adapter *a, u32 f)
{
    if (a->fence.reserved == f) a->fence.reserved = 0;
}

int gfx2_fence_done(struct gfx2_adapter *a, u32 f)
{
    return reached(a->fence.completed, f) ||
        (a->fence.failed_upto && reached(a->fence.failed_upto, f));
}

static int failed_fence(const struct gfx2_adapter *a, u32 f)
{
    /* Failure completion remains STALE when recovery advances completed. */
    return a->fence.failed_upto && reached(a->fence.failed_upto, f);
}

int gfx2_fence_wait_k(struct gfx2_adapter *a, u32 f, u32 dl_us)
{
    int rc;
    if (!f) return 0;
    if (a->state == GFX2_AD_OK) {
        rc = refresh(a);
        if (rc) return rc;
    }
    if (failed_fence(a, f)) return OS32_ERR_STALE;
    if (reached(a->fence.completed, f)) return 0;
    if (a->state != GFX2_AD_OK) return OS32_ERR_STALE;
    rc = a->be->fence_wait(a->ctx, f, dl_us);
    if (rc) { a->fence.failed_upto = a->fence.issued; gfx2_adapter_lost(a, 0); return rc; }
    rc = refresh(a);
    if (rc) return rc;
    if (failed_fence(a, f)) return OS32_ERR_STALE;
    return 0;
}

int gfx2_cpu_begin(struct gfx2_surf *s, u32 access, u32 dl_us)
{
    struct gfx2_adapter *a = gfx2_adapter_get(s->adapter);
    int rc;
    if (s->state == GFX2_ST_LOST) return OS32_ERR_STALE;
    rc = gfx2_fence_wait_k(a, s->last_write, dl_us);
    if (!rc && (access & GFX2_CPU_W)) rc = gfx2_fence_wait_k(a, s->last_read, dl_us);
    if (rc) return rc;
    if (gfx2_fence_done(a, s->last_write)) s->last_write = 0;
    if (gfx2_fence_done(a, s->last_read)) s->last_read = 0;
    return 0;
}

int gfx2_surf_quiesce(struct gfx2_surf *s, u32 dl_us)
{
    struct gfx2_adapter *a = gfx2_adapter_get(s->adapter);
    int rc = gfx2_fence_wait_k(a, s->last_write, dl_us);
    if (!rc) rc = gfx2_fence_wait_k(a, s->last_read, dl_us);
    return rc;
}

void gfx2_cpu_end(struct gfx2_surf *s, u32 access, const struct gfx2_rect *r)
{
    GfxSyncCtx *ctx;
    u32 mode;
    if (!(access & GFX2_CPU_W)) return;
    if ((s->flags & GFX2_SF_ALIAS) && !gfx2_sync_get(gfx2_adapter_get(s->adapter), s->head, &ctx, &mode)) {
        if (mode == GFXSYNC_DIRECT) { (void)gfxsync_flush(ctx); return; }
        if (mode == GFXSYNC_SHADOW) { gfx2_dirty_add(s, r); return; }
    }
    if (s->pool == GFX2_POOL_VRAM) arch_cpu_serialize();
}

int gfx2_fence_wait(u32 adapter, u32 f, u32 deadline_ms)
{
    struct gfx2_adapter *a = gfx2_adapter_get(adapter);
    u32 dl = deadline_ms > U32_MAX / 1000u ? U32_MAX : deadline_ms * 1000u;
    if (!a) return OS32_ERR_INVAL;
    return gfx2_fence_wait_k(a, f, dl);
}

int gfx2_fence_query(u32 adapter, u32 *completed, u32 *failed_upto)
{
    struct gfx2_adapter *a = gfx2_adapter_get(adapter);
    int rc = 0;
    if (!a || !completed || !failed_upto) return OS32_ERR_INVAL;
    gfx2_flip_poll(a);
    if (a->state == GFX2_AD_OK) rc = refresh(a);
    if (rc) return rc;
    sweep(a);
    gfx2_closing_sweep(a);
    *completed = a->fence.completed;
    *failed_upto = a->fence.failed_upto;
    return 0;
}
