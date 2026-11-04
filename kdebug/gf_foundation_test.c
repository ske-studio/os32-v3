/* C9 foreground observations. Owner acquisition and display policy are C9b. */
#include "gf_foundation_test.h"
#include "gfinit_bus.h"
#include "gf_stage_state.h"
#include "recover_gf.h"
#include "../hwdev/gfinit_mode.h"
#include "../hwdev/gf/gf1_test.h"
#include "../hwdev/gf/production.h"
#include "../drivers/video/geforce/geforce_module.h"
#include "../drivers/pci/pci.h"
#include "../lib/serial_diag.h"
#include "../lib/crc32_core.inc"
#include <stddef.h>
#ifdef HWDEV_MODULE
#include "../hwdev/generated/hwdev_import_macros.h"
#endif

#define GF1_MEASURE_MS 1000U
/* Existing PIT wait accepts less than one tick per call. */
#define GF1_WAIT_US RECOVER_US_PER_MS
#define GF1_PHASES 5U

struct gf1_phase {
    const char *name;
    int rc;
    u32 ms, command, pmc, crc_before, crc_after;
    struct gf1_test_reply reply;
    struct gf_irq_stats delta;
    bool snapshot, command_valid, pmc_valid, delta_valid, page;
    bool before_valid, after_valid;
};
struct gf1_observer {
    struct gf_foundation *f;
    const struct gf1_test_ops *ops;
    const struct recover_ops *base;
    struct recover *r;
    int (*read_row)(void *, u32, u8 *);
    void *row_owner;
    bool production, stopped;
    u32 test, heads;
    struct gf1_phase *phase;
};

static int module_call(struct gf1_observer *o, u32 op, u32 heads,
                       struct gf1_test_reply *reply)
{
    const struct gf1_test_request q = {sizeof(q), op, heads};
    return o->ops->call(o->f, &q, reply);
}

static int snapshot(struct gf1_observer *o, struct gf1_phase *p)
{
    int rc = module_call(o, GF1T_SNAPSHOT, 0, &p->reply);
    if (!rc) p->snapshot = true;
    return rc;
}

static int actual(struct gf1_observer *o, struct gf1_phase *p)
{
    /* A revoked status is deliberately a software snapshot only. */
    if (o->f->revoked) return o->test == GF1_STATUS ? GF1_OK : GF1_EPCI;
    if (o->production) {
        struct gf_production *owner = (void *)((u8 *)o->f -
                                      offsetof(struct gf_production, foundation));
        if (!owner->prepared || !owner->display.io.read) return GF1_EINVAL;
        p->command = pci_cfg_read16(RECOVER_GF_BUS, RECOVER_GF_DEV,
                                    RECOVER_GF_FN, RECOVER_COMMAND);
        p->command_valid = true;
        if (owner->display.io.read(owner->display.io.opaque, GF_MODE_REGISTER_BAR,
                                  RECOVER_PMC_ENABLE, sizeof(u32), &p->pmc))
            return o->f->revoked ? GF1_EPCI : GF1_EIO;
    } else {
        const struct gfinit_bus *bus = o->f->io.opaque;
        u32 bdf = bus->identity.bdf;
        p->command = bus->pci->read16(bdf >> 8, (bdf >> 3) & 31U,
                                     bdf & 7U, RECOVER_COMMAND);
        p->command_valid = true;
        if (o->r->state == REC_VERIFIED) return GF1_OK; /* STOP: no trial MMIO */
        struct recover_bus_access a;
        if (recover_target_address(&o->r->profile->targets[REC_GF_T_PMC_ENABLE],
                                   REC_READ, &a)) return GF1_EIO;
        a.key = REC_GF_T_PMC_ENABLE;
        if (o->r->ops->read(o->r->opaque, &a, &p->pmc))
            return o->f->revoked ? GF1_EPCI : GF1_EIO;
    }
    p->pmc_valid = true;
    return GF1_OK;
}

static int display_crc(struct gf1_observer *o, u32 *crc)
{
    u8 row[GF_MODE_WIDTH];
    u32 state = CRC32_INIT;
    for (u32 y = 0; y < GF_MODE_HEIGHT; y++) {
        int rc = o->read_row(o->row_owner, y, row);
        if (rc) return rc;
        state = crc32_core_update(state, row, sizeof(row));
    }
    *crc = crc32_core_final(state);
    return GF1_OK;
}

static int observe(struct recover *r, void *arg)
{
    struct gf1_observer *o = arg;
    struct gf1_phase *p = o->phase;
    struct gf1_test_reply before;
    u32 start = o->base->ticks(0);
    int rc = GF1_OK;
    if (o->test == GF1_VBLANK || (o->test == GF1_CYCLE && !o->stopped)) {
        rc = module_call(o, GF1T_HEADS, o->heads, &before);
        if (rc) goto done;
    }
    rc = snapshot(o, p);
    if (rc) goto done;
    if (o->test == GF1_INSTMEM) {
        rc = display_crc(o, &p->crc_before);
        if (rc) goto done;
        p->before_valid = true;
        rc = module_call(o, GF1T_INST_PAGE, 0, &p->reply);
        p->page = true;
        if (rc) goto done;
        rc = display_crc(o, &p->crc_after);
        if (rc) goto done;
        p->after_valid = true;
        if (p->crc_before != p->crc_after) rc = GF1_EIO;
    } else if (o->test == GF1_VBLANK || o->test == GF1_CYCLE) {
        before = p->reply;
        start = o->base->ticks(0); /* interval starts immediately after snapshot */
        while ((u32)(o->base->ticks(0) - start) < PIT_HZ) {
            if ((!o->production && r->state == REC_RUNNING && recover_safe_point(r)) ||
                recover_hal_wait_frame(0, GF1_WAIT_US)) {
                rc = o->f->revoked ? GF1_EPCI : GF1_EIO;
                goto done;
            }
        }
        rc = snapshot(o, p);
        if (rc) goto done;
        p->delta.entries = p->reply.stats.entries - before.stats.entries;
        p->delta.fifo = p->reply.stats.fifo - before.stats.fifo;
        p->delta.graph = p->reply.stats.graph - before.stats.graph;
        p->delta.vblank[0] = p->reply.stats.vblank[0] - before.stats.vblank[0];
        p->delta.vblank[1] = p->reply.stats.vblank[1] - before.stats.vblank[1];
        p->delta_valid = true;
        if ((o->heads ? !p->delta.vblank[0] : p->delta.vblank[0] != 0) ||
            p->delta.vblank[1]) rc = GF1_EIO;
    }
    if (!rc) rc = actual(o, p);
    if (!rc && p->command_valid && p->command != p->reply.command) rc = GF1_EPCI;
    if (!rc && p->pmc_valid && p->pmc != p->reply.pmc) rc = GF1_EIO;
    if (!rc && o->test == GF1_CYCLE && p->command !=
        (o->stopped ? RECOVER_COMMAND_STOP : RECOVER_COMMAND_RUN)) rc = GF1_EPCI;
done:
    p->ms = (u32)(o->base->ticks(0) - start) * GF1_MEASURE_MS / PIT_HZ;
    return p->rc = rc;
}

static int measure(struct gf1_observer *o, struct gf1_phase *p)
{
    o->phase = p;
    p->rc = GF1_OK;
    /* Only revoked status and verified STOP observations bypass a trial.
     * In particular, diagnostic CRC/INST_PAGE must stay inside its callback. */
    bool soft_status = o->test == GF1_STATUS && o->f->revoked;
    bool stopped_observation = o->r->state == REC_VERIFIED &&
        (o->test == GF1_STATUS || (o->test == GF1_CYCLE && o->stopped));
    if (!o->production && !soft_status && !stopped_observation) {
        if (trial_step(o->r, &o->r->token, true, observe, o)) {
            if (!p->rc) p->rc = o->f->revoked ? GF1_EPCI : GF1_EIO;
        }
    } else (void)observe(o->r, o);
    return p->rc;
}

static int restore_heads(struct recover *r, void *arg)
{
    struct gf1_observer *o = arg;
    struct gf1_test_reply reply;
    (void)r;
    return o->phase->rc = module_call(o, GF1T_HEADS, o->heads, &reply);
}

static int restore(struct gf1_observer *o, u32 heads)
{
    struct gf1_phase p = {0};
    struct gf1_observer local = *o;
    local.heads = heads;
    local.phase = &p;
    if (!o->production) {
        if (o->r->state != REC_PAUSED) return GF1_EIO;
        if (trial_step(o->r, &o->r->token, true, restore_heads, &local))
            return p.rc ? p.rc : GF1_EIO;
        return p.rc;
    }
    return restore_heads(o->r, &local);
}

static int display_phase(struct gf1_observer *o, struct gf1_phase *p,
                         int (*display)(bool), bool start)
{
    u32 begin = o->base->ticks(0);
    p->rc = display(start);
    p->ms = (u32)(o->base->ticks(0) - begin) * GF1_MEASURE_MS / PIT_HZ;
    return p->rc;
}

static void number(const char *key, bool valid, u32 value, bool hex)
{
    serial_puts_polled(key);
    if (!valid) serial_puts_polled("na");
    else if (hex) serial_diag_hex(value, 8);
    else serial_diag_u32(value);
}

static void emit(const struct gf1_observer *o, const struct gf1_phase *p,
                 u32 session, bool available)
{
    static const char *const cases[] = {"status", "instmem", "vblank", "cycle"};
    serial_puts_polled("GF1 case=");
    serial_puts_polled(o->test <= GF1_CYCLE ? cases[o->test] : "na");
    serial_puts_polled(o->production ? " owner=production" : " owner=diagnostic");
    number(" session=", true, session, false);
    serial_puts_polled(" phase="); serial_puts_polled(p->name);
    number(" ms=", true, p->ms, false);
    serial_puts_polled(" rc="); serial_diag_i32(p->rc);
    serial_puts_polled(" result=");
    serial_puts_polled(!available ? "unavailable" : p->rc ? "ng" : "ok");
    number(" command_expected=", p->snapshot, p->reply.command, true);
    number(" command_actual=", p->command_valid, p->command, true);
    number(" pmc_expected=", p->snapshot, p->reply.pmc, true);
    number(" pmc_actual=", p->pmc_valid, p->pmc, true);
    number(" flags=", p->snapshot, p->reply.flags, true);
    number(" delta_entries=", p->delta_valid, p->delta.entries, false);
    number(" delta_fifo=", p->delta_valid, p->delta.fifo, false);
    number(" delta_graph=", p->delta_valid, p->delta.graph, false);
    number(" delta_vblankA=", p->delta_valid, p->delta.vblank[0], false);
    number(" delta_vblankB=", p->delta_valid, p->delta.vblank[1], false);
    number(" unknown=", p->snapshot, p->reply.stats.unknown, true);
    number(" bytes=", p->page, p->reply.bytes, false);
    number(" bad_off=", p->page, p->reply.bad_off, true);
    number(" crc_before=", p->before_valid, p->crc_before, true);
    number(" crc_after=", p->after_valid, p->crc_after, true);
    serial_puts_polled("\r\n");
}

int gf_foundation_test(struct gf1_request *request, struct gf_foundation *f,
                       const struct recover_ops *base, int (*display)(bool),
                       int (*read_row)(void *, u32, u8 *), void *row_owner)
{
    if (!request) return GF1_EINVAL;
    const struct gf1_request q = *request;
    const struct gf1_test_ops *ops = geforce_foundation_ops();
    struct gf1_observer o = {.f=f, .ops=ops, .base=base, .r=recover_owner(),
        .read_row=read_row, .row_owner=row_owner, .test=q.test,
        .production=f && ops && f == ops->production};
    struct gf1_phase phases[GF1_PHASES] = {{.name="precheck", .rc=GF1_EINVAL}};
    u32 count = 1, heads = f ? f->wanted_heads : 0;
    bool available = f && ops && ops->call;
    int rc = GF1_EINVAL;
    if (q.size != sizeof(q) || q.test > GF1_CYCLE || q.session || q.rc) goto end;
    if (!available) goto end;
    if (!base || !base->ticks || (q.test == GF1_INSTMEM && !read_row) ||
        (q.test == GF1_CYCLE && !display)) goto end;
    if (q.test == GF1_CYCLE && (!f->running || f->revoked ||
        (!o.production && (o.r->state != REC_PAUSED ||
         o.r->expected_node != RECOVER_NONE || !o.r->gf_stage ||
         !o.r->gf_stage->initialized)))) goto end;
    if (q.test == GF1_STATUS || q.test == GF1_INSTMEM) {
        phases[0].name = q.test == GF1_STATUS ? "status" : "instmem";
        rc = measure(&o, &phases[0]);
    } else if (q.test == GF1_VBLANK) {
        static const char *const names[] = {"heads1", "heads0", "heads1_again"};
        for (u32 i = 0; i < 3; i++) {
            phases[i].name = names[i]; count = i + 1; o.heads = i != 1;
            rc = measure(&o, &phases[i]);
            if (rc) break;
        }
        int restored = restore(&o, heads);
        if (!rc) rc = phases[count-1].rc = restored;
    } else {
        phases[0].name = "run_before"; o.heads = 1;
        rc = measure(&o, &phases[0]);
        if (rc) goto end;
        phases[1].name = "display_stop"; count = 2;
        rc = display_phase(&o, &phases[1], display, false);
        if (rc) goto end;
        o.stopped = true;
        if (!o.production && (!o.r->gf_stage->s0_ok || !o.r->s0_verified ||
                              o.r->state != REC_VERIFIED)) {
            rc = phases[1].rc = GF1_ESTOP; goto end;
        }
        phases[2].name = "stop"; count = 3; o.heads = 0;
        rc = measure(&o, &phases[2]);
        if (rc) goto end;
        phases[3].name = "display_start"; count = 4;
        rc = display_phase(&o, &phases[3], display, true);
        if (rc) goto end;
        o.stopped = false;
        phases[4].name = "run_after"; count = 5; o.heads = 1;
        rc = measure(&o, &phases[4]);
        if (!rc) rc = phases[4].rc = restore(&o, heads);
    }
end:
    request->session = !o.production && o.r ? o.r->token.session : 0;
    request->rc = rc;
    for (u32 i = 0; i < count; i++) emit(&o, &phases[i], request->session, available);
    serial_puts_polled("GF1 end rc="); serial_diag_i32(rc); serial_puts_polled("\r\n");
    return rc;
}
