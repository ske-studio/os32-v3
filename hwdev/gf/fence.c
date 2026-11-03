/* Reference emission/read: nouveau nv10_fence.c:33-41,53-55;
 * nv17_fence.c:90-93 selects these operations. Absolute deadlines, epochs
 * and INIT/DRAIN routing follow GF_FEAT2_DESIGN sections 6 and the addendum.
 * Sequence distance and deadline horizon are each below 2^31 by contract. */
#include "command.h"

#define GF2_FENCE_BAR 0U
#define GF2_FENCE_WIDTH 4U
#define GF2_FENCE_REFERENCE \
    (GF2_USER_BASE + GF2_CHID * GF2_USER_STRIDE + GF2_USER_REFERENCE)

static int fence_state(struct gf2_chan *ch)
{
    int rc = gf2_fault_check(ch);
    if (rc) return rc;
    if (ch->state != GF2_INIT && ch->state != GF2_RUN &&
        ch->state != GF2_DRAIN) return GF2_ESTATE;
    return GF2_OK;
}

int gf2_fence_emit(struct gf2_chan *ch, struct gf2_fence *out, u32 deadline_us)
{
    int rc = fence_state(ch);
    u32 seq;

    if (rc) return rc;
    seq = ++ch->seq;
    if (ch->state == GF2_RUN)
        rc = gf2_emit(ch, GF2_REFERENCE_SUBCHANNEL, GF2_REFERENCE_METHOD,
                       false, &seq, 1, deadline_us);
    else
        rc = gf2_push_reference(ch, seq, deadline_us);
    if (rc) return rc;
    rc = gf2_kick(ch);
    if (rc) return rc;
    *out = (struct gf2_fence){ch->epoch, seq};
    return GF2_OK;
}

int gf2_fence_wait(struct gf2_chan *ch, struct gf2_fence fence, u32 deadline_us)
{
    struct gf_io *io = &ch->f->io;

    for (;;) {
        u32 now, observed, delay;
        int rc = fence_state(ch);
        if (rc) return rc;
        if (fence.epoch != ch->epoch) return GF2_EFAULT;
        if (io->timing.clock_us(io->opaque, &now)) return GF2_EIO;
        if ((i32)(now - deadline_us) >= 0) return GF2_ETIME;
        if (io->read(io->opaque, GF2_FENCE_BAR, GF2_FENCE_REFERENCE,
                     GF2_FENCE_WIDTH, &observed)) return GF2_EIO;
        if ((i32)(observed - fence.seq) >= 0) return GF2_OK;
        delay = deadline_us - now;
        if (delay > GF2_WAIT_MAX_US) delay = GF2_WAIT_MAX_US;
        if (io->timing.wait_us(io->opaque, delay)) return GF2_EIO;
    }
}
