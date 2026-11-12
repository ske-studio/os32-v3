#include "gf_v2.h"
#include "geforce_module.h"
#include "hwdev/gf/display/api.h"

const struct gf4_test_ops *gf_v2_disp(void)
{
    return geforce_context && geforce_context->display_out ? *geforce_context->display_out : NULL;
}

int gf_v2_deadline(struct gf2_chan *ch, u32 us, u32 *dl)
{
    u32 now;
    struct gf_io *io = &ch->f->io;
    if (!io->timing.clock_us) return GF2_EIO;
    int rc = io->timing.clock_us(io->opaque, &now);
    if (rc) return rc;
    *dl = now + us;
    return 0;
}

int gf_v2_rc(int rc)
{
    switch (rc) {
    case 0: return 0;
    case GF2_ETIME: case GF4_ETIME: return OS32_ERR_TIMEOUT;
    case GF2_ENOSPC: case GF4_ENOSPC: return OS32_ERR_NOSPC;
    case GF2_EINVAL: case GF3_EINVAL: case GF3_ERANGE: case GF3_EFORMAT:
    case GF4_EINVAL: case GF4_ERANGE: case GF4_EFORMAT: return OS32_ERR_INVAL;
    case GF3_EUNSUPPORTED: case GF4_EUNSUPPORTED: case GF4_ENODEV: return OS32_ERR_NOSYS;
    case GF3_EBUSY: case GF4_EBUSY: return OS32_ERR_BUSY;
    default: return OS32_ERR_IO;
    }
}

int gf_v2_start(struct gf_v2 *g, u32 dl)
{
    const struct gf4_test_ops *ops = gf_v2_disp();
    struct gf4_ctx *c = ops ? ops->ctx : NULL;
    int rc;
    g->lost = 1;
    if (!c || !c->ready || !c->accepting || !c->ch || !c->accel || c->ch->state != GF2_RUN ||
        !c->accel->ready || c->accel->epoch != c->ch->epoch) return GF3_ESTATE;
    if (g->epoch != c->ch->epoch) gf_v2_fence_reset(g);
    g->g3 = c->accel; g->ch = c->ch;
    gf3_range_query_set(g->g3, gf_v2_gart_range, g);
    rc = gf_v2_gart_start(g, dl);
    if (rc) return rc;
    g->epoch = c->ch->epoch; g->lost = 0;
    return 0;
}

int gf_v2_stop(void *ctx)
{
    struct gf_v2 *g = ctx;
    const struct gf4_test_ops *ops = gf_v2_disp();
    struct gf4_ctx *c = ops ? ops->ctx : NULL;
    struct gf2_fence f; u32 dl = 0; int rc;
    g->lost = 1;
    if (!g->ch || g->ch->state == GF2_OFF || g->ch->epoch != g->epoch) return 0;
    rc = gf2_fault_check(g->ch);
    if (!rc) rc = gf_v2_deadline(g->ch, GF_V2_STOP_US, &dl);
    if (!rc) rc = gf_v2_fence_idle(g, dl);
    if (!rc) rc = gf2_fence_emit(g->ch, &f, dl);
    if (!rc) rc = gf2_fence_wait(g->ch, f, dl);
    if (!rc) return 0;
    if (c && c->links && c->links->failed) (void)c->links->failed(c->links->owner, rc, dl);
    return g->ch->state == GF2_OFF ? 0 : OS32_ERR_IO;
}
