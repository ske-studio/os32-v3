/* C3 only: direct calls stand in for the C9 caller, never link its weak stub. */
#include "gfx2_int.h"

struct gfx2_surf gfx2_surfs[GFX2_MAX_SURFACES];
static struct gfx2_adapter adapter;
static u32 completed, failed, waits[4], wait_count, wait_dl;
static u32 serials, flushes, dirties, sync_mode, state_calls;
static u32 drains, closings, stops, policies, events[4], event_count;
static u32 expected_issued, expected_f, emit_calls;
static int emit_rc, wait_rc, state_rc, stop_rc, bad_order;
static const struct gfx2_rect *dirty_rect;

static void zero(void *p, u32 n)
{
    u8 *b = p;
    while (n--) *b++ = 0;
}

void gfx2_flip_poll(struct gfx2_adapter *a) { (void)a; }
void test_serialize(void) { serials++; }
struct gfx2_adapter *gfx2_adapter_get(u32 n) { return n == 0 ? &adapter : NULL; }
int gfx2_surf_state_set(struct gfx2_surf *s, u32 st) { s->state = st; return 0; }
void gfx2_quarantine_drain(struct gfx2_adapter *a)
{
    if (a == &adapter && a->state == GFX2_AD_OK) drains++;
}
void gfx2_closing_sweep(struct gfx2_adapter *a) { if (a == &adapter) closings++; }
void gfx_policy_reeval(void) { policies++; }
int gfx2_sync_get(const struct gfx2_adapter *a, u32 h, GfxSyncCtx **c, u32 *m)
{
    (void)a; (void)h;
    *c = (GfxSyncCtx *)1; *m = sync_mode;
    return sync_mode ? 0 : OS32_ERR_NOTFOUND;
}
int gfxsync_flush(GfxSyncCtx *c) { (void)c; flushes++; return 0; }
void gfx2_dirty_add(struct gfx2_surf *s, const struct gfx2_rect *r)
{
    (void)s; dirties++; dirty_rect = r;
}
static int state(void *c, u32 *d, u32 *f)
{
    (void)c; state_calls++; *d = completed; *f = failed; return state_rc;
}
static int wait_f(void *c, u32 f, u32 dl)
{
    (void)c;
    if (wait_count < 4) waits[wait_count] = f;
    wait_count++; wait_dl = dl;
    if (!wait_rc) completed = f;
    return wait_rc;
}
static int draw(void *c, const struct gfx2_exec *e, u32 f)
{
    (void)c; (void)e;
    if (adapter.fence.issued != expected_issued || f != expected_f) bad_order = 1;
    events[event_count++] = 1;
    return emit_rc;
}
static int emit(void *c, u32 f, u32 dl)
{
    (void)c; emit_calls++;
    if (adapter.fence.issued != expected_issued || f != expected_f ||
        dl != 1000000u || gfx2_surfs[0].last_write == f) bad_order = 1;
    events[event_count++] = 2;
    return emit_rc;
}
static int stop(void *c) { (void)c; stops++; return stop_rc; }
static const GfxBackendV2 be = {
    .emit = draw, .fence_state = state, .fence_wait = wait_f,
    .fence_emit = emit, .stop = stop
};

static void reset(void)
{
    zero(&adapter, sizeof(adapter));
    zero(gfx2_surfs, sizeof(gfx2_surfs));
    adapter.be = &be; adapter.gen = 7;
    gfx2_surfs[0].state = GFX2_ST_IDLE;
    completed = failed = wait_count = wait_dl = state_calls = 0;
    serials = flushes = dirties = sync_mode = drains = closings = 0;
    stops = policies = event_count = emit_calls = 0;
    expected_issued = expected_f = 0;
    emit_rc = wait_rc = state_rc = stop_rc = bad_order = 0;
    dirty_rect = NULL;
}

static int wrap(void)
{
    struct gfx2_surf *s = &gfx2_surfs[0];
    reset();
    completed = 0xfffffff8UL;
    s->last_write = 0xfffffff0UL; s->last_read = 0x10;
    if (gfx2_cpu_begin(s, GFX2_CPU_W, 123) || wait_count != 1 ||
        waits[0] != 0x10 || wait_dl != 123 || s->last_read || s->last_write) return 11;
    /* Equality is completion; zero never waits. */
    if (!gfx2_fence_done(&adapter, 0x10) || !gfx2_fence_done(&adapter, 0) ||
        gfx2_fence_wait_k(&adapter, 0, 123) || wait_count != 1) return 12;
    adapter.fence.issued = 0xffffffffUL;
    if (gfx2_fence_reserve(&adapter) != 1 || adapter.fence.issued != 0xffffffffUL ||
        gfx2_fence_reserve(&adapter) != 1) return 13;
    gfx2_fence_abandon(&adapter, 2);
    if (adapter.fence.reserved != 1) return 14;
    gfx2_fence_abandon(&adapter, 1);
    if (adapter.fence.reserved || gfx2_fence_reserve(&adapter) != 1) return 15;
    /* Two outstanding values must be waited in write/read order, never max. */
    reset(); s->last_write = 5; s->last_read = 9;
    if (gfx2_cpu_begin(s, GFX2_CPU_R | GFX2_CPU_W, 7) || wait_count != 2 ||
        waits[0] != 5 || waits[1] != 9) return 16;
    reset(); s->last_write = 5; s->last_read = 9;
    if (gfx2_cpu_begin(s, GFX2_CPU_R, 7) || wait_count != 1 ||
        waits[0] != 5 || s->last_write || s->last_read != 9) return 17;
    return 0;
}

static int failure(void)
{
    struct gfx2_surf *s = &gfx2_surfs[0];
    u32 d, f, calls;
    reset();
    adapter.fence.issued = 8; adapter.fence.completed = 4;
    s->last_write = 8; s->last_read = 6;
    gfx2_surfs[1].state = GFX2_ST_IDLE; gfx2_surfs[1].last_write = 4;
    gfx2_surfs[2].state = GFX2_ST_IDLE; gfx2_surfs[2].last_read = 8;
    gfx2_surfs[3].state = GFX2_ST_IDLE; gfx2_surfs[3].last_write = 9;
    gfx2_surfs[4].state = GFX2_ST_IDLE; gfx2_surfs[4].last_write = 8;
    gfx2_surfs[4].adapter = 1;
    /* Fake epoch change, including backend reporting done == failed. */
    completed = failed = 8;
    if (gfx2_fence_wait_k(&adapter, 8, 1) != OS32_ERR_STALE ||
        s->state != GFX2_ST_LOST || adapter.gen != 8 ||
        adapter.state != GFX2_AD_LOST || stops != 1 || policies != 1) return 21;
    if (gfx2_surfs[1].state != GFX2_ST_IDLE || gfx2_surfs[2].state != GFX2_ST_IDLE ||
        gfx2_surfs[3].state != GFX2_ST_IDLE || gfx2_surfs[4].state != GFX2_ST_IDLE ||
        s->last_write != 8 || s->last_read != 6 || !gfx2_fence_done(&adapter, 8)) return 22;
    if (gfx2_cpu_begin(s, GFX2_CPU_R, 1) != OS32_ERR_STALE) return 23;
    calls = state_calls;
    if (gfx2_fence_query(0, &d, &f) || f != 8 || state_calls != calls ||
        gfx2_fence_reserve(&adapter) || adapter.state != GFX2_AD_LOST) return 24;
    gfx2_adapter_lost(&adapter, 0);
    if (adapter.gen != 8 || stops != 1 || policies != 1) return 25;
    gfx2_adapter_recovered(&adapter);
    if (adapter.state != GFX2_AD_OK || drains != 1) return 26;
    /* Retain failure history after a successful newer fence completes. */
    expected_f = 9; expected_issued = 8; failed = 0;
    if (gfx2_fence_reserve(&adapter) != 9 ||
        gfx2_fence_commit(&adapter, 9, &gfx2_surfs[2], NULL)) return 27;
    completed = 9;
    if (gfx2_fence_wait_k(&adapter, 9, 1) ||
        gfx2_fence_wait_k(&adapter, 8, 1) != OS32_ERR_STALE ||
        gfx2_fence_query(0, &d, &f) || d != 9 || f != 8 ||
        gfx2_fence_wait(0, 8, 1) != OS32_ERR_STALE) return 28;
    return 0;
}

static int ordering(void)
{
    struct gfx2_surf *s = &gfx2_surfs[0], *src = &gfx2_surfs[1];
    u32 f;
    reset(); src->state = GFX2_ST_IDLE;
    adapter.fence.issued = expected_issued = 3; expected_f = 4;
    f = gfx2_fence_reserve(&adapter);
    if (f != 4 || adapter.fence.issued != 3 || event_count) return 31;
    /* C9's documented calls, not an implementation of submit/run_one. */
    if (be.emit(adapter.ctx, NULL, f) || gfx2_fence_commit(&adapter, f, s, src)) return 32;
    if (bad_order || event_count != 2 || events[0] != 1 || events[1] != 2 ||
        adapter.fence.issued != 4 || adapter.fence.reserved ||
        s->last_write != 4 || src->last_read != 4) return 33;
    if (gfx2_cpu_begin(s, GFX2_CPU_W, 55) || wait_count != 1 || waits[0] != 4) return 34;
    reset(); adapter.fence.issued = expected_issued = 3; expected_f = 4;
    emit_rc = OS32_ERR_IO;
    f = gfx2_fence_reserve(&adapter);
    if (gfx2_fence_commit(&adapter, f, s, src) != OS32_ERR_IO || bad_order ||
        adapter.fence.issued != 3 || adapter.fence.reserved ||
        adapter.fence.failed_upto != 4 || s->last_write != 4 || src->last_read != 4 ||
        s->state != GFX2_ST_LOST || src->state != GFX2_ST_FREE ||
        adapter.state != GFX2_AD_LOST) return 35;
    gfx2_adapter_recovered(&adapter);
    if (gfx2_fence_reserve(&adapter) != 5) return 36;
    reset(); adapter.fence.issued = 3;
    f = gfx2_fence_reserve(&adapter);
    gfx2_fence_commit_fail(&adapter, f, s, src, OS32_ERR_IO);
    if (emit_calls || adapter.fence.issued != 3 || adapter.fence.reserved ||
        adapter.fence.failed_upto != 4 || s->state != GFX2_ST_LOST ||
        s->last_write != 4 || src->last_read != 4 || adapter.gen != 8) return 37;
    return 0;
}

static int timeout_and_quiesce(void)
{
    struct gfx2_surf *s = &gfx2_surfs[0];
    reset(); adapter.fence.issued = 9; s->last_write = 5; s->last_read = 9;
    wait_rc = OS32_ERR_TIMEOUT; stop_rc = OS32_ERR_IO;
    if (gfx2_cpu_begin(s, GFX2_CPU_W, 17) != OS32_ERR_TIMEOUT ||
        wait_count != 1 || wait_dl != 17 || s->last_write != 5 || s->last_read != 9 ||
        adapter.state != GFX2_AD_LOST_HELD || adapter.gen != 8 ||
        adapter.fence.failed_upto != 9 || s->state != GFX2_ST_LOST) return 41;
    if (gfx2_surf_quiesce(s, 1) != OS32_ERR_STALE || wait_count != 1 ||
        s->last_write != 5 || s->last_read != 9) return 42;
    reset(); s->last_write = 5; s->last_read = 9;
    if (gfx2_surf_quiesce(s, 31) || wait_count != 2 || waits[0] != 5 || waits[1] != 9 ||
        s->last_write != 5 || s->last_read != 9) return 43;
    reset(); adapter.fence.issued = 9; s->last_write = 5;
    state_rc = OS32_ERR_IO;
    if (gfx2_fence_wait_k(&adapter, 5, 1) != OS32_ERR_IO ||
        adapter.fence.failed_upto != 9 || s->state != GFX2_ST_LOST || stops != 1) return 44;
    reset(); gfx2_adapter_lost(&adapter, 1);
    if (adapter.state != GFX2_AD_LOST_HELD || stops || policies != 1) return 45;
    reset();
    if (gfx2_fence_wait(0, 3, 7) || wait_dl != 7000) return 46;
    reset();
    if (gfx2_fence_wait(0, 3, U32_MAX) || wait_dl != U32_MAX ||
        gfx2_fence_wait(1, 0, 0) != OS32_ERR_INVAL) return 47;
    return 0;
}

static int flush(void)
{
    struct gfx2_surf *s = &gfx2_surfs[0];
    const struct gfx2_rect r = {1, 2, 3, 4};
    reset(); s->pool = GFX2_POOL_VRAM;
    gfx2_cpu_end(s, GFX2_CPU_R, &r);
    gfx2_cpu_end(s, GFX2_CPU_W, &r);
    if (serials != 1 || flushes || dirties) return 51;
    s->flags = GFX2_SF_ALIAS; sync_mode = GFXSYNC_DIRECT;
    gfx2_cpu_end(s, GFX2_CPU_R, &r);
    gfx2_cpu_end(s, GFX2_CPU_W, &r);
    if (flushes != 1 || serials != 1 || dirties) return 52;
    sync_mode = GFXSYNC_SHADOW;
    gfx2_cpu_end(s, GFX2_CPU_R, &r);
    gfx2_cpu_end(s, GFX2_CPU_R | GFX2_CPU_W, &r);
    if (dirties != 1 || dirty_rect != &r || flushes != 1 || serials != 1) return 53;
    sync_mode = 0;
    gfx2_cpu_end(s, GFX2_CPU_W, &r);
    if (serials != 2) return 54;
    s->flags = 0; s->pool = GFX2_POOL_GART;
    gfx2_cpu_end(s, GFX2_CPU_W, &r);
    s->pool = GFX2_POOL_SYSMEM; gfx2_cpu_end(s, GFX2_CPU_W, &r);
    if (serials != 2 || flushes != 1 || dirties != 1) return 55;
    return 0;
}

static int sweeping(void)
{
    struct gfx2_surf *s = &gfx2_surfs[0];
    u32 d, f;
    reset(); expected_issued = adapter.fence.issued = 0x3fffffffUL;
    expected_f = 0x40000000UL; completed = 0x3ffffffeUL;
    s->last_read = completed;
    gfx2_surfs[1].state = GFX2_ST_IDLE; gfx2_surfs[1].last_write = completed;
    gfx2_surfs[2].state = GFX2_ST_IDLE; gfx2_surfs[2].last_write = completed;
    gfx2_surfs[2].adapter = 1;
    if (gfx2_fence_commit(&adapter, gfx2_fence_reserve(&adapter), s, NULL) ||
        adapter.fence.last_sweep != expected_f || s->last_read ||
        s->last_write != expected_f || gfx2_surfs[1].last_write ||
        gfx2_surfs[2].last_write != completed || state_calls != 1) return 61;
    completed = expected_f;
    if (gfx2_fence_query(0, &d, &f) || d != expected_f || f || s->last_write ||
        closings != 1) return 62;
    d = 123; f = 456;
    if (gfx2_fence_query(1, &d, &f) != OS32_ERR_INVAL || d != 123 || f != 456 ||
        gfx2_fence_query(0, NULL, &f) != OS32_ERR_INVAL ||
        gfx2_fence_query(0, &d, NULL) != OS32_ERR_INVAL) return 63;
    return 0;
}

static int run(void)
{
    int rc = wrap();
    if (!rc) rc = failure();
    if (!rc) rc = ordering();
    if (!rc) rc = timeout_and_quiesce();
    if (!rc) rc = flush();
    if (!rc) rc = sweeping();
    return rc;
}
void _start(void)
{
    int rc = run();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
