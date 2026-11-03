/* T2 serial acceptance, through the module's typed diagnostic ops only.
 * GF_FEAT2_DESIGN section 9 and the T2 addenda define the observations. */
#include "gf_command_test.h"
#include "../hwdev/gf/command.h"
#include "../hwdev/gf/gf1_test.h"
#include "../drivers/video/geforce/geforce_module.h"
#include "../drivers/pci/pci.h"
#include "../include/io.h"
#include "../lib/serial_diag.h"
#include "../lib/crc32_core.inc"
#ifdef HWDEV_MODULE
#include "../hwdev/generated/hwdev_import_macros.h"
#endif

#define GF2_REFERENCE_COUNT 100000U
#define GF2_REFERENCE_BATCH 1000U
#define GF2_OBSERVE_US 1000000U
/* Read-only section-2 table observations (nouveau fifo/nv40.c:190-217). */
enum { GF2_OBSERVE_RAMHT = 0x2210, GF2_OBSERVE_RAMFC = 0x2220 };

struct gf2_observer {
    struct gf_foundation *f;
    const struct gf1_test_ops *foundation;
    const struct gf2_test_ops *ops;
    struct recover *r;
    bool production;
    int (*read_row)(void *, u32, u8 *);
    void *row_owner;
    enum gf2_test_op op;
    u32 count, completed, wraps, elapsed, previous_epoch;
    u32 crc_before, crc_after;
    bool crc_valid, snapshot, tables_valid, irq_enabled;
    u32 ramht, ramfc, device;
    struct gf2_test_reply reply;
    struct gf_irq_stats delta;
    int rc, call_rc;
};

static int clock_now(struct gf2_observer *o, u32 *now)
{
    return o->f->io.timing.clock_us(o->f->io.opaque, now) ? GF2_EIO : GF2_OK;
}
static int safe_point(struct gf2_observer *o)
{
    if (!o->production && o->r->state == REC_RUNNING && recover_safe_point(o->r))
        return GF2_EIO;
    return GF2_OK;
}
static int command_call(struct gf2_observer *o, enum gf2_test_op op)
{
    u32 now;
    int rc = clock_now(o, &now);
    if (rc) return rc;
    const struct gf2_test_request q = {sizeof(q), op, now + GF2_TIMEOUT_US};
    o->call_rc = o->ops->call(o->f, &q, &o->reply);
    o->snapshot = true;
    return o->call_rc;
}
static int display_crc(struct gf2_observer *o, u32 *crc)
{
    u8 row[GF_MODE_WIDTH];
    u32 state = CRC32_INIT;
    for (u32 y = 0; y < GF_MODE_HEIGHT; y++) {
        int rc = o->read_row(o->row_owner, y, row);
        if (rc) return rc;
        state = crc32_core_update(state, row, sizeof(row));
    }
    *crc = crc32_core_final(state);
    return GF2_OK;
}
static int reference_run(struct gf2_observer *o)
{
    int rc = command_call(o, GF2T_SNAPSHOT);
    if (rc) return rc;
    if (o->reply.state != GF2_RUN) return GF2_ESTATE;
    u32 put = o->reply.put, epoch = o->reply.epoch, seq = o->reply.seq;
    /* Each C9 trial has its existing finite poll budget. Keep that budget;
     * continue the same acceptance count over foreground trial calls. */
    u32 limit = o->completed + GF2_REFERENCE_BATCH;
    if (limit > o->count) limit = o->count;
    while (o->completed < limit) {
        rc = safe_point(o);
        if (!rc) rc = command_call(o, GF2T_REFERENCE);
        if (rc) return rc;
        o->irq_enabled = o->irq_enabled && _irq_enabled();
        if (!o->irq_enabled) return GF2_EIO;
        if (o->reply.state != GF2_RUN || o->reply.epoch != epoch ||
            o->reply.seq != seq + 1U ||
            (i32)(o->reply.reference - o->reply.seq) < 0) return GF2_EIO;
        if (o->reply.put < put) o->wraps++;
        put = o->reply.put; seq = o->reply.seq;
        o->completed++;
    }
    if (o->f->io.read(o->f->io.opaque, GF_MODE_REGISTER_BAR,
            GF2_OBSERVE_RAMHT, sizeof(u32), &o->ramht) ||
        o->f->io.read(o->f->io.opaque, GF_MODE_REGISTER_BAR,
            GF2_OBSERVE_RAMFC, sizeof(u32), &o->ramfc)) return GF2_EIO;
    o->tables_valid = true;
    return o->completed < o->count || o->wraps >= 2U ? GF2_OK : GF2_EIO;
}
static int heads(struct gf2_observer *o, u32 mask)
{
    const struct gf1_test_request q = {sizeof(q), GF1T_HEADS, mask};
    struct gf1_test_reply reply;
    return o->foundation->call(o->f, &q, &reply);
}
static int vblank(struct gf2_observer *o)
{
    u32 saved = o->f->wanted_heads, start = 0, now = 0;
    struct gf_irq_stats before;
    int rc = heads(o, 1U);
    if (rc) return rc;
    rc = command_call(o, GF2T_SNAPSHOT);
    before = o->reply.stats;
    if (!rc && o->reply.state != GF2_RUN) rc = GF2_ESTATE;
    if (!rc) rc = clock_now(o, &start);
    while (!rc) {
        rc = clock_now(o, &now);
        if (rc || (i32)(now - (start + GF2_OBSERVE_US)) >= 0) break;
        rc = safe_point(o);
        if (!rc && o->f->io.timing.wait_us(o->f->io.opaque, GF2_WAIT_MAX_US))
            rc = GF2_EIO;
    }
    if (!rc) rc = command_call(o, GF2T_SNAPSHOT);
    if (!rc) {
        o->delta.entries = o->reply.stats.entries - before.entries;
        o->delta.fifo = o->reply.stats.fifo - before.fifo;
        o->delta.graph = o->reply.stats.graph - before.graph;
        o->delta.vblank[0] = o->reply.stats.vblank[0] - before.vblank[0];
        o->delta.vblank[1] = o->reply.stats.vblank[1] - before.vblank[1];
        if (!o->delta.vblank[0] || o->delta.vblank[1] ||
            o->delta.fifo || o->delta.graph) rc = GF2_EIO;
    }
    int restored = heads(o, saved);
    return rc ? rc : restored;
}
static int observe(struct recover *r, void *arg)
{
    struct gf2_observer *o = arg;
    u32 start = 0, end;
    (void)r;
    int rc = clock_now(o, &start);
    if (rc) return o->rc = rc;
    if (o->count) rc = reference_run(o);
    else rc = command_call(o, o->op);
    /* A latched snapshot is a successful observation. Keep the diagnostic
     * lease paused so the following explicit recover can use that owner. */
    if (o->op == GF2T_SNAPSHOT && rc == GF2_EFAULT &&
        o->reply.state == GF2_FAULT && o->reply.latched) rc = GF2_OK;
    if (!rc && (o->op == GF2T_START || o->op == GF2T_RECOVER) &&
        (o->reply.state != GF2_RUN ||
         (i32)(o->reply.reference - o->reply.seq) < 0)) rc = GF2_EIO;
    if (!rc && o->op == GF2T_RECOVER && o->reply.epoch == o->previous_epoch)
        rc = GF2_EIO;
    if (!rc && o->op == GF2T_STOP && o->reply.state != GF2_OFF) rc = GF2_ESTOP;
    if (o->op == GF2T_BAD_HANDLE || o->op == GF2T_BAD_METHOD)
        rc = rc == GF2_EFAULT && o->reply.state == GF2_FAULT &&
             o->reply.latched && o->reply.fault_epoch == o->reply.epoch ?
             GF2_OK : rc ? rc : GF2_EIO;
    int timed = clock_now(o, &end);
    if (!timed) o->elapsed += end - start;
    else if (!rc) rc = timed;
    return o->rc = rc;
}
static int observe_vblank(struct recover *r, void *arg)
{
    struct gf2_observer *o = arg;
    (void)r;
    u32 start = 0, end;
    int rc = clock_now(o, &start);
    if (!rc) rc = vblank(o);
    int timed = clock_now(o, &end);
    o->elapsed = timed ? 0 : end - start;
    if (!rc) rc = timed;
    return o->rc = rc;
}
static int observe_crc(struct recover *r, void *arg)
{
    struct gf2_observer *o = arg;
    (void)r;
    o->elapsed = 0;
    return o->rc = display_crc(o, &o->crc_after);
}
static int measure(struct gf2_observer *o, int (*body)(struct recover *, void *))
{
    o->rc = GF2_OK;
    if (!o->production && !o->f->revoked) {
        if (o->r->state != REC_PAUSED) return o->rc = GF2_ESTATE;
        if (trial_step(o->r, &o->r->token, true, body, o) && !o->rc)
            o->rc = GF2_EIO;
    } else (void)body(o->r, o);
    return o->rc;
}
static void number(const char *key, u32 value, bool hex)
{
    serial_puts_polled(key);
    if (hex) serial_diag_hex(value, 8);
    else serial_diag_u32(value);
}
static void stats(const char *key, const struct gf_irq_stats *s)
{
    serial_puts_polled(key);
    number(" entries=", s->entries, false);
    number(" fifo=", s->fifo, false); number(" graph=", s->graph, false);
    number(" vblankA=", s->vblank[0], false); number(" vblankB=", s->vblank[1], false);
    number(" pmc=", s->pmc, true); number(" pfifo=", s->pfifo, true);
    number(" pgraph=", s->pgraph, true); number(" nsource=", s->nsource, true);
    number(" nstatus=", s->nstatus, true); number(" unknown=", s->unknown, true);
}
static void emit(struct gf2_observer *o, const char *phase, u32 session, bool available)
{
    const struct gf2_test_reply *p = &o->reply;
    serial_puts_polled("GF2 phase="); serial_puts_polled(phase);
    serial_puts_polled(o->production ? " owner=production" : " owner=diagnostic");
    number(" session=", session, false);
    number(" bdf=", RECOVER_GF_BDF, true); number(" device=", o->device, true);
    serial_puts_polled(" rc="); serial_diag_i32(o->rc);
    serial_puts_polled(" call_rc="); serial_diag_i32(o->call_rc);
    serial_puts_polled(" result=");
    serial_puts_polled(!available ? "unavailable" : o->rc ? "ng" : "ok");
    number(" snapshot=", o->snapshot, false); number(" us=", o->elapsed, false);
    number(" irq_enabled=", o->irq_enabled, false);
    number(" completed=", o->completed, false); number(" wraps=", o->wraps, false);
    number(" state=", p->state, false); number(" epoch=", p->epoch, false);
    number(" seq=", p->seq, false); number(" PUT=", p->put, true);
    number(" GET=", p->get, true); number(" reference=", p->reference, true);
    number(" B=", p->B, true); number(" ctx.off=", p->ctx_off, true);
    number(" ctx_bytes=", p->ctx_bytes, false); number(" ctxprog_words=", p->ctx_words, false);
    serial_puts_polled(" dma_rc="); serial_diag_i32(p->dma_rc);
    if (o->f) {
        number(" V=", o->f->imem.vram_bytes, true);
        number(" R=", o->f->imem.reserved, true);
    }
    if (o->tables_valid) {
        number(" reg2210=", o->ramht, true); number(" reg2220=", o->ramfc, true);
    }
    if (o->crc_valid) {
        number(" crc_before=", o->crc_before, true); number(" crc_after=", o->crc_after, true);
    }
    stats(" current", &p->stats); stats(" delta", &o->delta);
    if (p->latched) {
        number(" fault_epoch=", p->fault_epoch, false); number(" chid=", p->chid, false);
        number(" fault_PUT=", p->fault_put, true); number(" fault_GET=", p->fault_get, true);
        number(" fault_reference=", p->fault_reference, true);
        number(" caches=", p->caches, true); number(" push1=", p->push1, true);
        number(" cache_get=", p->cache_get, true); number(" pull0=", p->pull0, true);
        number(" method=", p->method, true); number(" data=", p->data, true);
        number(" dma_state=", p->dma_state, true); number(" dma_fetch=", p->dma_fetch, true);
        number(" dma_push=", p->dma_push, true); number(" dma_dcount=", p->dma_dcount, true);
        number(" gr_trap=", p->gr_trap, true); number(" gr_method=", p->gr_method, true);
        number(" subc=", (p->gr_trap >> 16) & GF2_SUBCHANNEL_MAX, false);
        number(" gr_data=", p->gr_data, true); number(" gr_class=", p->gr_class, true);
        stats(" first", &p->first); stats(" latest", &p->latest);
    }
    serial_puts_polled("\r\n");
}
static int phase(struct gf2_observer *o, enum gf2_test_op op, const char *name, u32 session)
{
    o->op = op; o->count = op == GF2T_REFERENCE ? GF2_REFERENCE_COUNT : 0;
    o->completed = o->wraps = o->elapsed = 0;
    o->snapshot = false; o->tables_valid = false; o->call_rc = 0;
    o->irq_enabled = _irq_enabled() != 0;
    int rc;
    do { rc = measure(o, observe); }
    while (!rc && o->completed < o->count);
    emit(o, name, session, true);
    return rc;
}
int gf_command_test(struct gf2_request *request, struct gf_foundation *f,
                    int (*read_row)(void *, u32, u8 *), void *row_owner)
{
    if (!request) return GF2_EINVAL;
    const struct gf2_request q = *request;
    const struct gf1_test_ops *foundation = geforce_foundation_ops();
    struct gf2_observer o = {.f=f, .foundation=foundation,
        .ops=foundation ? foundation->command : 0, .r=recover_owner(),
        .production=f && foundation && f == foundation->production,
        .irq_enabled=_irq_enabled() != 0,
        .read_row=read_row, .row_owner=row_owner};
    bool available = f && o.ops && o.ops->call;
    u32 session = !o.production && o.r ? o.r->token.session : 0;
    int rc = GF2_EINVAL;
    if (q.size != sizeof(q) || q.test > GF2_VBLANK || q.session || q.rc ||
        !available || !f->io.timing.clock_us || !f->io.timing.wait_us) goto rejected;
    if (!f->revoked) {
        o.device = pci_cfg_read32(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN,
                                 PCI_CFG_VENDOR_ID);
        if (o.device != RECOVER_GF_DEVICE) { rc = GF1_EPCI; goto rejected; }
    }
    if (o.production && (q.test == GF2_BAD_HANDLE || q.test == GF2_BAD_METHOD))
        goto rejected;
    if (q.test == GF2_STATUS) rc = phase(&o, GF2T_SNAPSHOT, "status", session);
    else if (q.test == GF2_START) rc = phase(&o, GF2T_START, "start", session);
    else if (q.test == GF2_STOP) rc = phase(&o, GF2T_STOP, "stop", session);
    else if (q.test == GF2_RECOVER) {
        rc = phase(&o, GF2T_SNAPSHOT, "before_recover", session);
        if (!rc) {
            o.previous_epoch = o.reply.epoch;
            rc = phase(&o, GF2T_RECOVER, "recover", session);
        }
    } else if (q.test == GF2_REFERENCE) rc = phase(&o, GF2T_REFERENCE, "reference", session);
    else if (q.test == GF2_VBLANK) {
        rc = measure(&o, observe_vblank); emit(&o, "vblank", session, true);
    } else if (q.test == GF2_BAD_HANDLE || q.test == GF2_BAD_METHOD) {
        rc = phase(&o, q.test == GF2_BAD_HANDLE ? GF2T_BAD_HANDLE : GF2T_BAD_METHOD,
                   q.test == GF2_BAD_HANDLE ? "bad_handle" : "bad_method", session);
        if (!rc) {
            o.previous_epoch = o.reply.epoch;
            rc = phase(&o, GF2T_RECOVER, "explicit_recover", session);
        }
        if (!rc) rc = phase(&o, GF2T_REFERENCE, "reference_after_fault", session);
    } else {
        if (!read_row) goto rejected;
        rc = measure(&o, observe_crc);
        if (rc) goto end;
        o.crc_before = o.crc_after;
        rc = phase(&o, GF2T_SNAPSHOT, "before_start", session);
        if (!rc && o.reply.state == GF2_OFF) rc = phase(&o, GF2T_START, "start", session);
        if (!rc) rc = phase(&o, GF2T_REFERENCE, "reference_before_stop", session);
        if (!rc) rc = phase(&o, GF2T_STOP, "stop", session);
        if (!rc) {
            o.previous_epoch = o.reply.epoch;
            rc = phase(&o, GF2T_RECOVER, "restart", session);
        }
        if (!rc) rc = phase(&o, GF2T_REFERENCE, "reference_after_restart", session);
        if (!rc) { rc = measure(&o, observe_vblank); emit(&o, "vblank", session, true); }
        if (!rc) {
            rc = measure(&o, observe_crc);
            o.crc_valid = !rc;
            if (!rc && o.crc_before != o.crc_after) rc = o.rc = GF2_EIO;
            emit(&o, "display", session, true);
        }
    }
    goto end;
rejected:
    o.rc = rc; emit(&o, "precheck", session, available);
end:
    request->session = session; request->rc = rc;
    serial_puts_polled("GF2 end rc="); serial_diag_i32(rc); serial_puts_polled("\r\n");
    return rc;
}
