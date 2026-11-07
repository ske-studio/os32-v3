/* NV04 display flip: nouveau dispnv04/crtc.c:1074-1087,1136-1217;
 * dispnv04/hw.c:702-710, hw.h:353-357, nvreg.h:299. */
#include "api.h"
#include "../../../include/sys.h"
#include "../../../lib/serial_diag.h"

#define FLIP_BAR 0U
#define FLIP_START 0x600800U
#define FLIP_STATUS 0x6013daU
#define FLIP_HEAD_STRIDE 0x2000U
#define FLIP_RETRACE 0x08U
#define FLIP_WORD 4U
#define FLIP_BYTE 1U

/* Diagnostic samples use the same 64-bit microsecond clock as F5 clock64.
 * Clock failures are reported, without changing the flip result or retrying. */
struct flip_time { u32 lo, hi; int rc; };

static void sample(struct flip_time *t)
{
    t->rc = sys_time_now(&t->lo, &t->hi);
}

static void stamp(const char *name, const struct flip_time *t)
{
    serial_puts_polled(name);
    serial_diag_hex(t->hi, 8);
    serial_diag_hex(t->lo, 8);
    serial_puts_polled(" clock_rc="); serial_diag_i32(t->rc);
}

static void diagnostic(u8 head, bool sync, const struct flip_time *edge,
                       const struct flip_time *pre, const struct flip_time *post,
                       int rc)
{
    if (serial_gate_active()) return;
    serial_puts_polled("[gf4] flip head="); serial_diag_u32(head);
    serial_puts_polled(" sync="); serial_diag_u32(sync);
    if (sync) stamp(" edge_us=0x", edge);
    stamp(" pre_us=0x", pre);
    stamp(" post_us=0x", post);
    serial_puts_polled(" rc="); serial_diag_i32(rc);
    serial_puts_polled("\r\n");
}

static int valid(struct gf4_ctx *c, u8 head)
{
    if (!c || head >= GF4_HEADS) return GF4_EINVAL;
    if (!c->ch || !c->ch->f || !c->state || !c->ready ||
        c->epoch != c->ch->epoch || !c->ch->f->running ||
        !c->state->head[head].enabled || c->state->head[head].dpms != GF4_ON ||
        !(c->ch->heads & (1U << head)) ||
        !(c->ch->f->wanted_heads & (1U << head))) return GF4_ESTATE;
    return 0;
}

static int time_left(struct gf4_ctx *c, u32 dl, u32 *now)
{
    struct gf_io *io = &c->ch->f->io;
    int rc = io->timing.clock_us(io->opaque, now);
    if (rc) return rc;
    return (i32)(*now - dl) >= 0 ? GF4_ETIME : 0;
}

static int tick(struct gf4_ctx *c, u8 head, u32 dl)
{
    struct gf_io *io = &c->ch->f->io;
    u32 now, delay;
    int rc = valid(c, head);
    if (rc) return rc;
    rc = gf2_fault_check(c->ch);
    if (rc) return rc;
    rc = time_left(c, dl, &now);
    if (rc) return rc;
    delay = dl - now;
    if (delay > GF2_WAIT_MAX_US) delay = GF2_WAIT_MAX_US;
    return io->timing.wait_us(io->opaque, delay);
}

static u32 counter(struct gf4_ctx *c, u8 head)
{
    struct gf_irq_stats s;
    gf_irq_snapshot(c->ch->f, &s);
    return s.vblank[head];
}

static int wait_counter(struct gf4_ctx *c, u8 head, u32 after, u32 dl, u32 *out)
{
    for (;;) {
        int rc = valid(c, head);
        u32 n, now;
        if (rc) return rc;
        rc = gf2_fault_check(c->ch);
        if (rc) return rc;
        rc = time_left(c, dl, &now);
        if (rc) return rc;
        n = counter(c, head);
        if ((u32)(n - after)) { *out = n; return 0; }
        rc = tick(c, head, dl);
        if (rc) return rc;
    }
}

static int commit(struct gf4_ctx *c, u8 head, const struct gf3_surface *s)
{
    int rc = c->links->scan_changed(c->links->owner, head, s);
    if (rc) return rc;
    c->state->head[head].front = *s;
    c->hold[head] = (struct gf4_flip_hold){0};
    return 0;
}

i32 gf4_flip_complete(struct gf4_ctx *c, u8 head, u32 dl)
{
    int rc = valid(c, head);
    struct gf4_flip_hold *h;
    struct gf_io *io;
    u32 value, seen;
    if (rc) return rc;
    h = &c->hold[head];
    if (!h->pending) return 0;
    if (!c->links || !c->links->scan_changed) return GF4_ESTATE;
    io = &c->ch->f->io;
    if (!h->base_verified) {
        if (h->old_front.off == h->new_front.off)
            return commit(c, head, &h->new_front);
        rc = io->read(io->opaque, FLIP_BAR, FLIP_START + head * FLIP_HEAD_STRIDE,
                      FLIP_WORD, &value);
        if (rc) return rc;
        if (value == h->old_front.off) {
            rc = commit(c, head, &h->old_front);
            return rc ? rc : GF4_ESTATE;
        }
        if (value != h->new_front.off) return GF4_ESTATE;
        h->after = counter(c, head);
        h->base_verified = true;
    }
    rc = wait_counter(c, head, h->after, dl, &seen);
    if (rc) return rc;
    return commit(c, head, &h->new_front);
}

int gf4_vblank_wait(struct gf4_ctx *c, u8 head, u32 after, u32 dl, u32 *seen)
{
    int rc;
    u32 n;
    if (!seen) return GF4_EINVAL;
    rc = valid(c, head);
    if (rc) return rc;
    if (c->hold[head].pending) {
        rc = gf4_flip_complete(c, head, dl);
        if (rc) return rc;
    }
    rc = wait_counter(c, head, after, dl, &n);
    if (!rc) *seen = n;
    return rc;
}

int gf4_flip(struct gf4_ctx *c, u8 head, const struct gf3_surface *front,
             bool sync, u32 dl)
{
    struct gf4_flip_hold *h;
    struct gf_io *io;
    struct gf2_fence fence;
    u32 before, seen, status, readback;
    struct flip_time edge = {0}, pre = {0}, post = {0};
    int rc;
    if (!front) return GF4_EINVAL;
    rc = valid(c, head);
    if (rc) return rc;
    if (!c->accepting || !c->links || !c->links->scan_changed) return GF4_ESTATE;
    h = &c->hold[head];
    if (h->pending) return GF4_EBUSY;
    rc = gf4_surface_check(c, front, &c->state->head[head].mode);
    if (rc) return rc;
    rc = gf2_fence_emit(c->ch, &fence, dl);
    if (rc) return rc;
    rc = gf2_fence_wait(c->ch, fence, dl);
    if (rc) return rc;
    io = &c->ch->f->io;
    if (sync) {
        before = counter(c, head);
        rc = wait_counter(c, head, before, dl, &seen);
        if (rc) return rc;
        for (;;) {
            u32 now;
            rc = time_left(c, dl, &now);
            if (rc) return rc;
            rc = io->read(io->opaque, FLIP_BAR, FLIP_STATUS + head * FLIP_HEAD_STRIDE,
                          FLIP_BYTE, &status);
            if (rc) return rc;
            if (status & FLIP_RETRACE) { sample(&edge); break; }
            rc = tick(c, head, dl);
            if (rc) return rc;
        }
    }
    h->old_front = c->state->head[head].front;
    h->new_front = *front;
    h->pending = h->base_written = true;
    h->base_verified = false;
    sample(&pre);
    rc = io->write(io->opaque, FLIP_BAR, FLIP_START + head * FLIP_HEAD_STRIDE,
                   FLIP_WORD, front->off);
    sample(&post);
    if (rc) goto done;
    rc = io->read(io->opaque, FLIP_BAR, FLIP_START + head * FLIP_HEAD_STRIDE,
                  FLIP_WORD, &readback);
    if (rc) goto done;
    if (readback != front->off) { rc = GF4_ESTATE; goto done; }
    h->after = counter(c, head);
    h->base_verified = true;
    rc = sync ? gf4_flip_complete(c, head, dl) : 0;
done:
    /* Serial output follows completion, including its final vblank wait. */
    diagnostic(head, sync, &edge, &pre, &post, rc);
    return rc;
}
