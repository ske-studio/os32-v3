/* T0c. MIT nouveau c03c1a1 therm/temp.c:104-158 supplies edge
 * notifications; GF_GUARD_THERM_DESIGN section 6 supplies OS32 pause ownership.
 * Use real production init, FIFO links and existing scanlog hardware seams. */
#define GF4_SCANLOG_ENTRY critical_unused_scanlog_start
#include "gf4_scanlog_host.c"
#undef GF4_SCANLOG_ENTRY

static i32 (*critical_drain)(void *),(*critical_resume)(void *);
static u32 critical_drains,critical_resumes;
static i32 critical_drain_rc,critical_resume_rc;
static i32 critical_count_drain(void *owner)
{
    ++critical_drains;
    return critical_drain_rc ? critical_drain_rc : critical_drain(owner);
}
static i32 critical_count_resume(void *owner)
{
    ++critical_resumes;
    return critical_resume_rc ? critical_resume_rc : critical_resume(owner);
}
static void critical_setup(void)
{
    scanlog_init();
    struct gf5 *g=&cint_prod.feature5;
    assert(g->emergency.pause && g->emergency.owner==g);
    assert(!scanlog_f5_ops->service_poll(g));
    critical_drain=g->links.drain_pause;
    critical_resume=g->links.unpause;
    g->links.drain_pause=critical_count_drain;
    g->links.unpause=critical_count_resume;
    critical_drains=critical_resumes=0;
    critical_drain_rc=critical_resume_rc=0;
}
static void critical_edge(struct gf5 *g,bool active)
{
    /* therm invokes the callback while the foreground worker owns busy. */
    g->busy=true;
    g->emergency.pause(g->emergency.owner,active);
    g->busy=false;
}
static void critical_roundtrip(void)
{
    critical_setup();
    struct gf5 *g=&cint_prod.feature5;
    /* Actual sensor edge, with no frame rendering. 105 C is the ROM default
     * critical threshold (therm/temp.c defaults), 5 C its hysteresis. */
    gf3_host_set(&cint_host,0,0x15b4,105);
    cint_host.now=(u32)g->next_sensor;
    c1_clear_trace();
    assert(!scanlog_f5_ops->service_poll(g));
    assert(critical_drains==1 && g->heat.critical_paused && !g->accepting);
    assert(!c1_normal && !svc_slot);
    /* FIFO-only pause: scanout and the display route stay running. */
    assert(active && routed && scanlog_route && g->display->ready);
    assert(!svc_host.calls[D_WRITE]);
    struct gf5_clock clock;
    assert(scanlog_f5_ops->clock_read(g,&clock)==GF5_ESTATE);
    critical_edge(g,true);
    assert(critical_drains==1);
    gf3_host_set(&cint_host,0,0x15b4,101);
    cint_host.now=(u32)g->next_sensor;
    assert(!scanlog_f5_ops->service_poll(g));
    assert(critical_resumes==0 && g->heat.critical_paused);
    /* Below the release threshold, independent of <= versus < edge policy. */
    gf3_host_set(&cint_host,0,0x15b4,99);
    cint_host.now=(u32)g->next_sensor;
    assert(!scanlog_f5_ops->service_poll(g));
    assert(critical_resumes==1 && !g->heat.critical_paused && g->accepting);
    assert(c1_normal && svc_slot && active && routed);
    assert(!scanlog_f5_ops->clock_read(g,&clock));
    critical_edge(g,false);
    assert(critical_resumes==1);
    scanlog_finish();
}
static void critical_ownership(void)
{
    for (u32 i=0;i<7;++i) {
        critical_setup();
        struct gf5 *g=&cint_prod.feature5;
        critical_edge(g,true);
        assert(critical_drains==1);
        switch (i) {
        case 0: g->phase=GF5_SAVED; break;
        case 1: g->phase=GF5_FAULT; break;
        case 2: g->display->saved_closed=true; break;
        case 3: g->display->changing=true; break;
        case 4: g->ch->fault.latched=true; break;
        case 5: g->heat.stop_pending=true; break;
        case 6: ++g->heat.epoch; break;
        }
        critical_edge(g,false);
        assert(!critical_resumes && !g->accepting && !c1_normal);
        g->phase=GF5_RUN; g->display->saved_closed=g->display->changing=false;
        g->ch->fault.latched=g->heat.stop_pending=false;
        g->heat.epoch=g->ch->epoch;
        critical_edge(g,false);
        assert(critical_resumes==1 && g->accepting);
        scanlog_finish();
    }
    critical_setup();
    struct gf5 *g=&cint_prod.feature5;
    /* A pre-existing closure grants no critical pause ownership. */
    assert(!g->links.admission(g,false));
    critical_edge(g,true); critical_edge(g,false);
    assert(!critical_drains && !critical_resumes && !g->accepting);
    scanlog_finish();
}
static void critical_failures(void)
{
    for (u32 i=0;i<4;++i) {
        critical_setup();
        struct gf5 *g=&cint_prod.feature5;
        if (i==0) critical_drain_rc=SVC_INJECT;
        if (i==1) scanlog_clock_error=true;
        critical_edge(g,true);
        if (i>=2) {
            assert(g->heat.critical_paused);
            if (i==2) critical_resume_rc=SVC_INJECT;
            else svc_open_rc=SVC_INJECT;
            critical_edge(g,false);
        }
        scanlog_clock_error=false; svc_open_rc=0;
        assert(g->phase==GF5_FAULT && !g->accepting && g->heat.stop_pending);
        assert(!c1_normal && !svc_slot && g->heat.last_rc==GF5_EIO);
        assert(g->source_rc==(i==1 ? -901 : SVC_INJECT));
        /* Management survives and the same failure never retries resume. */
        assert(scanlog_f5_ops->last_rc(g)==GF5_EIO);
        u32 resumes=critical_resumes;
        critical_edge(g,false);
        assert(resumes==critical_resumes);
        critical_drain_rc=critical_resume_rc=0;
        scanlog_finish();
    }
}
void _start(void)
{
    scanlog_map(CINT_FB_PHYS,GF_MODE_VRAM_BYTES);
    scanlog_map(SCANLOG_BAR0,0x101000U);
    critical_roundtrip();
    critical_ownership();
    critical_failures();
    gf2_host_exit(0);
}
