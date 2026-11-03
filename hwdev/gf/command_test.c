/* Foreground layer-2 diagnostic bridge, GF_FEAT2_DESIGN T2 addenda.
 * The fixed BAD_* streams are OS32 fixtures: nouveau include/nvhw/class/
 * cl0039.h:27-44 and nvkm/engine/gr/nv40.c:455 define their class/methods.
 * Owner selection and lease cleanup stay with C9/T2b. */
#include "command.h"
#include "gf2_test.h"

#define TEST_BAD_HANDLE 0xdeadbeefU
#define TEST_METHOD_HANDLE 0x80000100U
#define TEST_BIND_METHOD 0U
#define TEST_BAD_METHOD 0x1ffcU
#define TEST_SUBCHANNEL 0U
#define TEST_USER (GF2_USER_BASE + GF2_CHID * GF2_USER_STRIDE)

static struct gf2_chan diagnostic;
static struct gf_foundation *production_f;
static struct gf2_chan *production_ch;
static struct gf2_test_reply diagnostic_saved, production_saved;
/* The allocator links inst by address. Only C0 release_instances reclaims
 * this object; neither prepare nor a failed call may clear or move it. */
static struct gf2_object test_object;
static u32 test_object_epoch;

void gf2_test_bind_production(struct gf_foundation *f, struct gf2_chan *ch)
{
    production_f = f;
    production_ch = ch;
}

static int prepare(struct gf_foundation *f,
                    const struct recover_bar_extent *bars, u32 n)
{
    if (!f || !bars || n != RECOVER_BAR_COUNT || f == production_f ||
        f->revoked || !bars[GF_MODE_VRAM_BAR].valid || !f->map_ops ||
        !f->io.timing.clock_us || !f->io.timing.wait_us)
        return GF2_EINVAL;
    /* OFF is published only after successful resource release. Preserve
     * the channel and epoch across preparations, especially failed stop. */
    if (diagnostic.state != GF2_OFF) return GF2_ESTATE;
    diagnostic.f = f;
    diagnostic.bar1 = bars[GF_MODE_VRAM_BAR];
    diagnostic.map_ops = f->map_ops;
    diagnostic.map_owner = f->map_owner;
    diagnostic.dma_ops = geforce_dma_ops();
    diagnostic_saved = (struct gf2_test_reply){0};
    return GF2_OK;
}

static int reference(struct gf2_chan *ch, u32 deadline)
{
    struct gf2_fence fence;
    int rc = gf2_fence_emit(ch, &fence, deadline);
    if (!rc) rc = gf2_fence_wait(ch, fence, deadline);
    return rc;
}

static int wait_fault(struct gf2_chan *ch, u32 deadline)
{
    struct gf_io *io = &ch->f->io;
    for (;;) {
        u32 now, delay;
        int rc = gf2_fault_check(ch);
        if (rc) return rc;
        if (io->timing.clock_us(io->opaque, &now)) return GF2_EIO;
        if ((i32)(now - deadline) >= 0) return GF2_ETIME;
        delay = deadline - now;
        if (delay > GF2_WAIT_MAX_US) delay = GF2_WAIT_MAX_US;
        if (io->timing.wait_us(io->opaque, delay)) return GF2_EIO;
    }
}

static int bad_command(struct gf2_chan *ch, enum gf2_test_op op, u32 deadline)
{
    u32 handle = TEST_BAD_HANDLE, data = 0;
    int rc;
    if (ch != &diagnostic || ch->state != GF2_RUN) return GF2_ESTATE;
    rc = gf2_fault_check(ch);
    if (rc) return rc;
    if (op == GF2T_BAD_METHOD) {
        handle = TEST_METHOD_HANDLE;
        if (!test_object.handle || test_object_epoch != ch->epoch) {
            /* Section 4: drain and pause before publishing a RAMHT entry. */
            rc = reference(ch, deadline);
            if (rc) return rc;
            rc = gf2_fifo_pause(ch);
            if (rc) return rc;
            rc = gf2_gr_object_new(ch, handle, GF2_GR_M2MF, &test_object);
            if (rc) return rc;
            test_object_epoch = ch->epoch;
            ch->last_rc = GF2_OK;
            gf2_fifo_resume(ch);
            if (ch->last_rc) return ch->last_rc;
        }
    }
    rc = gf2_emit(ch, TEST_SUBCHANNEL, TEST_BIND_METHOD, false,
                   &handle, 1, deadline);
    if (rc) return rc;
    if (op == GF2T_BAD_METHOD) {
        rc = gf2_emit(ch, TEST_SUBCHANNEL, TEST_BAD_METHOD, false,
                       &data, 1, deadline);
        if (rc) return rc;
    }
    rc = gf2_kick(ch);
    if (rc) return rc;
    return wait_fault(ch, deadline);
}

static int snapshot(struct gf_foundation *f, struct gf2_chan *ch,
                      struct gf2_test_reply *reply)
{
    const struct gf2_fault *fault = &ch->fault;
    if (!ch->grctx.size) reply->ctx_words = 0;
    else if (reply->epoch != ch->epoch || !reply->ctx_words) {
        u32 code[GF2_GRCTX_PROGRAM_WORDS], bytes;
        int rc = gf2_grctx_program(code, &reply->ctx_words, &bytes);
        if (rc) return rc;
    }
    reply->dma_rc = ch->dma_rc;
    reply->state = ch->state;
    reply->epoch = ch->epoch;
    reply->seq = ch->seq;
    reply->B = ch->B;
    reply->ctx_off = ch->grctx.off;
    reply->ctx_bytes = ch->grctx.size;
    reply->latched = fault->latched;
    reply->fault_epoch = fault->epoch;
    reply->chid = fault->chid;
    reply->fault_put = fault->put;
    reply->fault_get = fault->get;
    reply->fault_reference = fault->reference;
    reply->first = fault->first;
    reply->latest = fault->latest;
    reply->caches = fault->caches;
    reply->push1 = fault->push1;
    reply->cache_get = fault->cache_get;
    reply->pull0 = fault->pull0;
    reply->method = fault->method;
    reply->data = fault->data;
    reply->dma_state = fault->dma_state;
    reply->dma_fetch = fault->dma_fetch;
    reply->dma_push = fault->dma_push;
    reply->dma_dcount = fault->dma_dcount;
    reply->gr_trap = fault->gr_trap;
    reply->gr_method = fault->gr_method;
    reply->gr_data = fault->gr_data;
    reply->gr_class = fault->gr_class;
    gf_irq_snapshot(f, &reply->stats);
    /* userd layout: nouveau nvkm/engine/fifo/nv40.c:95-99;
     * REFERENCE: nouveau nv10_fence.c:53-55. No access after F1 stop. */
    if (ch->state != GF2_OFF && f->running) {
        struct gf_io *io = &f->io;
        if (io->read(io->opaque, GF_MODE_REGISTER_BAR,
                      TEST_USER + GF2_USER_PUT, sizeof(u32), &reply->put) ||
            io->read(io->opaque, GF_MODE_REGISTER_BAR,
                      TEST_USER + GF2_USER_GET, sizeof(u32), &reply->get) ||
            io->read(io->opaque, GF_MODE_REGISTER_BAR,
                      TEST_USER + GF2_USER_REFERENCE, sizeof(u32), &reply->reference))
            return GF2_EIO;
    }
    return GF2_OK;
}

static int call(struct gf_foundation *f, const struct gf2_test_request *q,
                 struct gf2_test_reply *reply)
{
    struct gf2_chan *ch;
    struct gf2_test_reply *saved;
    int rc = GF2_OK;
    if (!reply) return GF2_EINVAL;
    *reply = (struct gf2_test_reply){.rc = GF2_EINVAL};
    if (!f || !q || q->size != sizeof(*q) ||
        (u32)q->op > GF2T_BAD_METHOD) return reply->rc;
    if (f == production_f && production_ch) {
        ch = production_ch;
        saved = &production_saved;
    } else if (f == diagnostic.f) {
        ch = &diagnostic;
        saved = &diagnostic_saved;
    } else return reply->rc;
    *reply = *saved;
    /* Revocation forbids every hardware operation, including fault capture
     * and stop/recover. The lease owner retains resources for its cleanup. */
    if (f->revoked) return reply->rc = GF2_EFAULT;
    switch (q->op) {
    case GF2T_START: rc = gf2_start(ch, f); break;
    case GF2T_REFERENCE:
        rc = ch->f == f && ch->state != GF2_OFF ?
             reference(ch, q->deadline_us) : GF2_ESTATE;
        break;
    case GF2T_SNAPSHOT:
        if (ch->state != GF2_OFF && f->running) rc = gf2_fault_check(ch);
        break;
    case GF2T_STOP: rc = gf2_stop(ch); break;
    case GF2T_RECOVER:
        rc = ch->f == f ? gf2_recover(ch) : GF2_ESTATE;
        break;
    case GF2T_BAD_HANDLE:
    case GF2T_BAD_METHOD: rc = bad_command(ch, q->op, q->deadline_us); break;
    }
    int observed = snapshot(f, ch, reply);
    reply->rc = rc ? rc : observed;
    *saved = *reply;
    return reply->rc;
}

const struct gf2_test_ops gf2_module_ops = {.prepare = prepare, .call = call};
