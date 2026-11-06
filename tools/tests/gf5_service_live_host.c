/* A10 production assembly, with the scanlog PCI/MMIO/allocator seams.
 * MIT nouveau c03c1a1 therm/base.c:343-357, clk/base.c:591-598:
 * monitoring lifetime is independent of drawing admission. */
#define GF4_SCANLOG_ENTRY service_scanlog_unused_start
#include "gf4_scanlog_host.c"
#undef GF4_SCANLOG_ENTRY

static void live_rejects(void)
{
    struct gf5 *g=&cint_prod.feature5;
    struct gf2_chan *ch=g->ch;
    struct gf_foundation *f=ch->f;
    struct gf4_ctx *d=g->display;
    struct gf5_thermal sample;
    for (u32 i=0;i<17;++i) {
        switch (i) {
        case 0: g->ch=0; break;
        case 1: ch->f=0; break;
        case 2: ch->state=GF2_OFF; break;
        case 3: ch->fault.latched=true; break;
        case 4: f->running=false; break;
        case 5: g->accepting=false; break;
        case 6: d->ready=false; break;
        case 7: d->accepting=false; break;
        case 8: d->changing=true; break;
        case 9: d->saved_closed=true; break;
        case 10: ++d->epoch; break;
        case 11: g->phase=GF5_SAVED; break;
        case 12: g->phase=GF5_FAULT; break;
        case 13: g->phase=GF5_SAVING; break;
        case 14: g->phase=GF5_RESTORING; break;
        case 15: g->display=0; break;
        case 16: ch->epoch=0; break;
        }
        g->plan_guard.valid=true;
        c1_clear_trace();
        assert(scanlog_f5_ops->therm_read(g,&sample)==GF5_ESTATE);
        assert(!cint_host.calls[H_READ] && !cint_host.calls[H_WRITE]);
        assert(!g->plan_guard.valid);
        g->ch=ch; ch->f=f; ch->state=GF2_RUN; ch->fault.latched=false;
        f->running=true; g->accepting=true; g->display=d;
        d->ready=d->accepting=true; d->changing=d->saved_closed=false;
        ch->epoch=d->epoch=1; g->phase=GF5_RUN;
    }
    g->busy=true;
    c1_clear_trace();
    assert(scanlog_f5_ops->therm_read(g,&sample)==GF5_EBUSY);
    assert(!scanlog_f5_ops->service_poll(g));
    scanlog_f5_ops->fan_tick(g,cint_host.now);
    assert(!cint_host.calls[H_READ] && !cint_host.calls[H_WRITE]);
    g->busy=false;
    const struct gf5_test_ops *borrowed=scanlog_f5_ops;
    scanlog_f5_ops=0; /* save closes publication before its read-only capture. */
    assert(borrowed->therm_read(g,&sample)==GF5_EBUSY);
    assert(borrowed->state_restore(g,0)==GF5_EBUSY);
    assert(!borrowed->service_poll(g));
    borrowed->fan_tick(g,cint_host.now);
    assert(!cint_host.calls[H_READ] && !cint_host.calls[H_WRITE]);
    scanlog_f5_ops=borrowed;
    assert(!scanlog_f5_ops->therm_read(g,&sample));
    assert(!g->busy && cint_host.calls[H_READ]);
    struct gf5 other={0};
    assert(scanlog_f5_ops->therm_read(&other,&sample)==GF5_ESTATE);
}

static void thermal_lifetime(void)
{
    struct gf5 *g=&cint_prod.feature5;
    struct gf_foundation *f=g->ch->f;
    assert(!scanlog_f5_ops->service_poll(g)); /* RUN initializes sensor. */
    assert(g->fan.sensor_setup && g->fan.sensor_poll);
    svc_slot=0; /* The simulated closed display publishes no drawing ops. */
    for (u32 i=0;i<2;++i) {
        g->phase=i ? GF5_SAVED : GF5_FAULT;
        g->ch->fault.latched=true; f->running=false;
        g->ch->state=GF2_OFF;
        g->accepting=g->display->ready=g->display->accepting=false;
        g->display->saved_closed=true;
        cint_host.now=(u32)g->next_sensor;
        c1_clear_trace();
        assert(!scanlog_f5_ops->service_poll(g));
        assert(cint_host.calls[H_READ] && g->next_sensor>cint_host.now);
        cint_host.now=(u32)g->next_sensor;
        c1_clear_trace();
        scanlog_f5_ops->fan_tick(g,cint_host.now);
        assert(cint_host.calls[H_READ] && !g->busy);
    }
    void *bar=g->display->legacy->bar0_map;
    for (u32 i=0;i<4;++i) {
        switch (i) {
        case 0: g->display->legacy->bar0_map=0; break;
        case 1: f->bar0=0; break;
        case 2: f->revoked=true; break;
        case 3: ++g->display->epoch; break;
        }
        cint_host.now=(u32)g->next_sensor;
        c1_clear_trace();
        assert(!scanlog_f5_ops->service_poll(g));
        scanlog_f5_ops->fan_tick(g,cint_host.now);
        assert(!cint_host.calls[H_READ] && !cint_host.calls[H_WRITE]);
        g->display->legacy->bar0_map=bar; f->bar0=bar; f->revoked=false;
        g->display->epoch=g->ch->epoch;
    }
    /* Management does not require a healthy/running channel. */
    for (u32 i=0;i<2;++i) {
        g->phase=i ? GF5_SAVED : GF5_FAULT;
        g->ch=0;
        void *allocation=0;
        assert(!g->links.alloc(g, sizeof(struct gf5_image), &allocation));
        g->image=allocation;
        g->last_rc=-903;
        assert(scanlog_f5_ops->last_rc(g)==-903);
        scanlog_f5_ops->state_free(g,allocation);
        assert(!g->image);
        assert(scanlog_f5_ops->state_restore(g,0)==GF5_EINVAL);
        g->ch=&cint_prod.chan;
    }
    g->phase=GF5_RUN; g->accepting=true; g->ch->state=GF2_RUN;
    g->ch->fault.latched=false; f->running=true;
    g->display->ready=g->display->accepting=true;
    g->display->saved_closed=false;
    g->plan_guard.valid=true;
    assert(scanlog_f5_ops->state_save(g,0)==GF5_EINVAL);
    assert(!g->plan_guard.valid && scanlog_f5_ops);
}

static void (*live_old_observe)(struct gf3_host *,const struct gf3_host_event *);
static bool live_in_observe;
static u32 live_reentries;
static void live_observe(struct gf3_host *h,const struct gf3_host_event *e)
{
    if (live_old_observe) live_old_observe(h,e);
    if (live_in_observe || e->op!=H_READ) return;
    live_in_observe=true;
    struct gf5 *g=&cint_prod.feature5;
    struct gf5_thermal sample;
    u32 reads=h->calls[H_READ],writes=h->calls[H_WRITE];
    assert(scanlog_f5_ops->therm_read(g,&sample)==GF5_EBUSY);
    assert(!scanlog_f5_ops->service_poll(g));
    assert(reads==h->calls[H_READ] && writes==h->calls[H_WRITE]);
    ++live_reentries;
    live_in_observe=false;
}
void _start(void)
{
    scanlog_map(CINT_FB_PHYS,GF_MODE_VRAM_BYTES);
    scanlog_map(SCANLOG_BAR0,0x101000U);
    scanlog_init();
    live_rejects();
    thermal_lifetime();
    live_old_observe=cint_host.observe;
    cint_host.observe=live_observe;
    struct gf5_thermal sample;
    assert(!scanlog_f5_ops->therm_read(&cint_prod.feature5,&sample));
    assert(live_reentries && !cint_prod.feature5.busy);
    cint_host.observe=live_old_observe;
    scanlog_finish();
    gf2_host_exit(0);
}
