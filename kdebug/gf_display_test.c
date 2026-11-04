/* GF_FEAT4_DESIGN final 2026-11-04 addendum, A3 wire -> session.
 * OS32's staged camera protocol has no nouveau equivalent. This slice
 * prepares the session helpers; case dispatch/READY belongs to later slices.
 * No register access, image allocation, or reservation release here. */
#include "gf_display_test.h"
#include "../hwdev/gf/display/api.h"
#include "../drivers/video/geforce/geforce_module.h"

/* Frozen addendum declarations; C3 owns their eventual shared-header lines. */
extern const struct gf4_test_ops *geforce_display_ops(void);
extern const struct gf4_restore_ops *geforce_display_restore_ops(void);

/* Wire bits are fixed by A3-case, rather than hardware fields. */
#define DISPLAY_BYTE_MASK 0xffU
#define DISPLAY_CONNECTOR_SHIFT 8U
#define DISPLAY_HEAD_SHIFT 16U
#define DISPLAY_PHASE_SHIFT 17U
#define DISPLAY_SESSION_MASK 0x3ffffU
enum display_case { DISPLAY_ENUM, DISPLAY_MODE, DISPLAY_DPMS, DISPLAY_CURSOR,
    DISPLAY_FLIP, DISPLAY_GAMMA, DISPLAY_OUTPUTS, DISPLAY_LIFECYCLE,
    DISPLAY_RESTORE };
struct display_request { u8 test, connector, head; bool phase; };
static struct {
    struct gf4_saved saved;
    struct display_request request;
    bool valid, running;
} display_session;

static inline i32 display_decode(u32 wire, struct display_request *out)
{
    if ((wire & ~DISPLAY_SESSION_MASK) ||
        (wire & DISPLAY_BYTE_MASK) > DISPLAY_RESTORE) return GF4_EINVAL;
    *out = (struct display_request){
        .test = wire & DISPLAY_BYTE_MASK,
        .connector = (wire >> DISPLAY_CONNECTOR_SHIFT) & DISPLAY_BYTE_MASK,
        .head = (wire >> DISPLAY_HEAD_SHIFT) & 1U,
        .phase = (wire >> DISPLAY_PHASE_SHIFT) & 1U
    };
    return 0;
}
/* A saved image survives both restore and saved_release failures. A session
 * cannot advance after a failed initial restore/normal-slot publication. */
static inline i32 display_admit(const struct display_request *q)
{
    if (q->test == DISPLAY_RESTORE) return 0;
    if (!display_session.valid) return q->phase ? GF4_ESTATE : 0;
    const struct display_request *old = &display_session.request;
    if (!q->phase || q->test != old->test || q->connector != old->connector ||
        q->head != old->head) return GF4_EBUSY;
    return display_session.running ? 0 : GF4_ESTATE;
}
static inline i32 display_deadline(struct gf4_ctx *ctx, u32 *dl)
{
    if (!ctx || !ctx->ch || !ctx->ch->f) return GF4_ESTATE;
    struct gf_foundation *f = ctx->ch->f;
    u32 now;
    if (!f->io.timing.clock_us) return GF4_ESTATE;
    i32 rc = f->io.timing.clock_us(f->io.opaque, &now);
    if (rc) return rc;
    /* F2/F3's existing bounded foreground operation budget. */
    *dl = now + GF3_TIMEOUT_US;
    return 0;
}
static inline i32 display_restore(bool finish)
{
    if (!display_session.valid) return 0;
    display_session.running = false;
    const struct gf4_restore_ops *o = geforce_display_restore_ops();
    u32 dl;
    if (!o || !o->restore) return GF4_ESTATE;
    i32 rc = display_deadline(o->ctx, &dl);
    if (rc) return rc;
    /* clock and each module call may invalidate a borrowed slot. */
    o = geforce_display_restore_ops();
    if (!o || !o->ctx || !o->restore) return GF4_ESTATE;
    rc = o->restore(o->ctx, &display_session.saved, dl);
    if (rc) return rc;
    if (finish) {
        o = geforce_display_restore_ops();
        if (!o || !o->ctx || !o->saved_release) return GF4_ESTATE;
        rc = o->saved_release(o->ctx, &display_session.saved);
        if (rc) return rc;
        display_session.valid = false;
    } else {
        const struct gf4_test_ops *normal = geforce_display_ops();
        if (!normal || !normal->ctx) return GF4_ESTATE;
        display_session.running = true;
    }
    return 0;
}
static inline i32 display_begin(const struct display_request *q)
{
    i32 rc = display_admit(q);
    if (rc) return rc;
    if (q->test == DISPLAY_ENUM || q->test == DISPLAY_RESTORE || q->phase)
        return GF4_EINVAL; /* Only image-case phase0 starts a snapshot. */
    const struct gf4_test_ops *o = geforce_display_ops();
    u32 dl;
    if (!o || !o->ctx || !o->display_save) return GF4_ESTATE;
    /* Final addendum (b): F5 owns a closed save without diagnostic_saved. */
    if (o->ctx->saved_closed && !o->ctx->diagnostic_saved) return GF4_ESTATE;
    rc = display_deadline(o->ctx, &dl);
    if (rc) return rc;
    o = geforce_display_ops();
    if (!o || !o->ctx || !o->display_save) return GF4_ESTATE;
    if (o->ctx->saved_closed && !o->ctx->diagnostic_saved) return GF4_ESTATE;
    rc = o->display_save(o->ctx, &display_session.saved, dl);
    if (rc) return rc;
    display_session.request = *q;
    display_session.valid = true;
    return display_restore(false);
}
