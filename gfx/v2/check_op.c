/* HAL v2 operation capability selection. */
#include "gfx2_sw.h"

static int visible(const struct gfx2_adapter *a, const struct gfx2_surf *s, int shape)
{
    if (!s) return 1;
    if (!shape) return gfx2_cpu_visible(s);
    return s->pool == GFX2_POOL_SYSMEM || s->pool == GFX2_POOL_GART ||
           (s->pool == GFX2_POOL_VRAM && a->caps.vram_cpu_bytes != 0);
}

static int shadow(struct gfx2_adapter *a, const struct gfx2_surf *s)
{
    GfxSyncCtx *ctx;
    u32 mode;
    return s && (s->flags & GFX2_SF_ALIAS) &&
           !gfx2_sync_get(a, s->head, &ctx, &mode) && mode == GFXSYNC_SHADOW;
}

static int check(struct gfx2_adapter *a, const struct gfx2_exec *ex, u32 *how, int shape)
{
    const GFX2_Op *op;
    u32 sf, df;
    if (!a || !ex || !ex->op || !how) return OS32_ERR_INVAL;
    op = ex->op;
    if (!op->op || op->op > GFX2_OP_MAX) return OS32_ERR_INVAL;
    sf = ex->src ? ex->src->fmt : 0;
    df = ex->dst ? ex->dst->fmt : 0;
    if ((ex->src && sf > GFX2_FMT_UYVY) || (ex->dst && df > GFX2_FMT_UYVY))
        return OS32_ERR_INVAL;
    *how = GFX2_HOW_NOSUPPORT;
    if ((ex->src && sf == GFX2_FMT_PLANAR4) || (ex->dst && df == GFX2_FMT_PLANAR4) ||
        (ex->src && ex->dst && sf != GFX2_FMT_INDEX8 && df == GFX2_FMT_INDEX8) ||
        (ex->dst && df >= GFX2_FMT_YUY2) ||
        (ex->src && sf >= GFX2_FMT_YUY2 && op->op != GFX2_OP_STRETCH) ||
        (op->op == GFX2_OP_STRETCH && (op->flags & GFX2_F_FILTER) && df == GFX2_FMT_INDEX8))
        return 0;
    if (a->state == GFX2_AD_OK && shadow(a, ex->dst)) {
        *how = GFX2_HOW_SW;
        return 0;
    }
    if (a->state == GFX2_AD_OK && !a->be->check(a->ctx, ex)) {
        *how = GFX2_HOW_HW;
        return 0;
    }
    if (visible(a, ex->src, shape) && visible(a, ex->dst, shape))
        *how = GFX2_HOW_SW;
    return 0;
}

int gfx2_check_exec(struct gfx2_adapter *a, const struct gfx2_exec *ex, u32 *how)
{
    return check(a, ex, how, 0);
}

int gfx2_check_op(u32 adapter, const GFX2_Op *op, u32 sf, u32 df,
                  u32 sp, u32 dp, u32 *how)
{
    struct gfx2_adapter *a = gfx2_adapter_get(adapter);
    struct gfx2_surf src = {0}, dst = {0};
    struct gfx2_pattern pat = {0};
    struct gfx2_exec ex = {0};
    int has_src, has_dst;
    if (!a || !op || !how || sf > GFX2_FMT_UYVY || df > GFX2_FMT_UYVY)
        return OS32_ERR_INVAL;
    has_src = op->op == GFX2_OP_COPY || op->op == GFX2_OP_STRETCH ||
              op->op == GFX2_OP_READBACK || op->op == GFX2_OP_ROP4 || op->op == GFX2_OP_ALPHA;
    has_dst = op->op != GFX2_OP_READBACK && op->op != GFX2_OP_SET_PATTERN;
    if ((has_src && (sp < GFX2_POOL_VRAM || sp > GFX2_POOL_SYSMEM)) ||
        (has_dst && (dp < GFX2_POOL_VRAM || dp > GFX2_POOL_SYSMEM))) return OS32_ERR_INVAL;
    src.fmt = sf; src.pool = sp; src.w = op->sw; src.h = op->sh;
    dst.fmt = df; dst.pool = dp; dst.w = op->dw; dst.h = op->dh;
    ex.op = op; ex.src = has_src ? &src : 0; ex.dst = has_dst ? &dst : 0;
    if (op->flags & GFX2_F_PAT) {
        pat.kind = (op->flags & GFX2_F_PAT_COLOR) ? 2 : 1;
        ex.pat = &pat;
    }
    return check(a, &ex, how, 1);
}
