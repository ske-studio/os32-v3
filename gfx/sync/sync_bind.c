/* HAL v2 sync context binding (HAL_V2_DESIGN §10). */
#include "sync_int.h"
#include "cpu_calibrate.h"
#include "kprintf.h"

static struct gfxsync_cfg defaults(void)
{
    struct gfxsync_cfg c = { .mode = GFXSYNC_SHADOW, .unit = 1,
        .method = GFXSYNC_M_PLAIN, .flush = GFXSYNC_F_NONE,
        .read = GFXSYNC_R_SHADOW };
    return c;
}

int gfxsync_cfg_check(const struct gfxsync_cfg *c, const GfxSyncTarget *t)
{
    if (!c || !t || (c->mode != GFXSYNC_SHADOW && c->mode != GFXSYNC_DIRECT) ||
        (c->unit != 1 && c->unit != 2 && c->unit != 4) ||
        c->method > GFXSYNC_M_WAIT_PORT || c->flush > GFXSYNC_F_SERIALIZE ||
        (c->read != GFXSYNC_R_SHADOW && c->read != GFXSYNC_R_DIRECT) ||
        c->gap_ns > 100000 ||
        (c->method == GFXSYNC_M_SYNC_READ && !t->sync_read) ||
        (c->method == GFXSYNC_M_WAIT_PORT && !t->wait_port) ||
        (c->flush == GFXSYNC_F_READBACK && !t->flush) ||
        (c->mode == GFXSYNC_DIRECT &&
         (c->read == GFXSYNC_R_SHADOW || !t->read32)))
        return OS32_ERR_INVAL;
    return 0;
}

int gfxsync_bind(const char *key, const GfxSyncTarget *t, GfxSyncCtx **out)
{
    struct gfxsync_cfg cfg = defaults();
    GfxSyncCtx *ctx = 0;
    int rc;
    if (!key || !t || !out || !t->va || !t->bytes || !t->pitch)
        return OS32_ERR_INVAL;
    for (u32 i = 0; i < 4; i++)
        if (!gfxsync_ctx_pool[i].used) { ctx = &gfxsync_ctx_pool[i]; break; }
    if (!ctx) return OS32_ERR_FULL;
    rc = gfxsync_cfg_lookup(key, &cfg);
    if (rc && rc != OS32_ERR_NOTFOUND) {
        cfg = defaults();
        kprintf(7, "gfxsync: invalid configuration; using defaults\n");
    }
    if (cfg.read == GFXSYNC_R_DIRECT && !t->read32 && cfg.mode == GFXSYNC_SHADOW)
        cfg.read = GFXSYNC_R_SHADOW;
    if (gfxsync_cfg_check(&cfg, t)) {
        cfg = defaults();
        kprintf(7, "gfxsync: invalid configuration; using defaults\n");
    }
    ctx->t = *t;
    ctx->cfg = cfg;
    ctx->bind_cfg = cfg;
    ctx->gap_loops = gfxsync_gap_loops(cpu_loops_per_tick(), cfg.gap_ns);
    ctx->used = 1;
    *out = ctx;
    return 0;
}

void gfxsync_unbind(GfxSyncCtx *ctx)
{
    if (ctx) ctx->used = 0;
}

int gfxsync_mode(const GfxSyncCtx *ctx)
{
    return ctx && ctx->used ? ctx->cfg.mode : 0;
}

void gfxsync_profile(const GfxSyncCtx *ctx, char out[16])
{
    if (!out) return;
    for (u32 i = 0; i < 16; i++)
        out[i] = ctx && ctx->used ? ctx->cfg.key[i] : 0;
    out[15] = 0;
}

int gfxsync_override(GfxSyncCtx *ctx, u32 method, u32 unit, u32 gap)
{
    struct gfxsync_cfg cfg;
    if (!ctx || !ctx->used || method > GFXSYNC_M_WAIT_PORT ||
        (unit != 1 && unit != 2 && unit != 4) || gap > 100000)
        return OS32_ERR_INVAL;
    cfg = ctx->cfg;
    cfg.method = method;
    cfg.unit = unit;
    cfg.gap_ns = gap;
    if (gfxsync_cfg_check(&cfg, &ctx->t)) return OS32_ERR_INVAL;
    ctx->cfg = cfg;
    ctx->gap_loops = gfxsync_gap_loops(cpu_loops_per_tick(), gap);
    return 0;
}

void gfxsync_override_clear(GfxSyncCtx *ctx)
{
    if (!ctx || !ctx->used) return;
    ctx->cfg = ctx->bind_cfg;
    ctx->gap_loops = gfxsync_gap_loops(cpu_loops_per_tick(), ctx->cfg.gap_ns);
}
