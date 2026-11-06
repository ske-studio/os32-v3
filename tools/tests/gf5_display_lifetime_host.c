/* L1: real production F4/F5 assembly over PCI/MMIO/time host seams.
 * MIT nouveau c03c1a1 therm/base.c:343-357, clk/base.c:591-598 end
 * workers with the device lifetime. OS32's public plans must expire before
 * stop side effects; heat ceilings/pending shutdown survive a restart
 * (GF_GUARD_THERM_DESIGN sections 2, 4, 6). No reference tree needed at runtime. */
#define GF4_SCANLOG_ENTRY lifetime_unused_start
#include "gf4_scanlog_host.c"
#undef GF4_SCANLOG_ENTRY

static void lifetime_seed(struct gf5 *g)
{
    g->plan_guard.valid=true;
    g->heat=(struct gf5_heat_state){
        .down_active=true,.critical_paused=true,.stop_pending=true,
        .cool_samples=2,.epoch=g->ch->epoch,
        .entry_clock={300000,300000,200000},
        .ceiling={200000,200000,200000},.last_rc=-901
    };
}

static void lifetime_preserved(struct gf5 *g)
{
    assert(g->heat.down_active && g->heat.stop_pending);
    assert(g->heat.entry_clock.core_khz==300000 && g->heat.entry_clock.shader_khz==300000);
    assert(g->heat.entry_clock.mem_khz==200000 && g->heat.ceiling.core_khz==200000);
    assert(g->heat.ceiling.shader_khz==200000 && g->heat.ceiling.mem_khz==200000);
    assert(g->heat.last_rc==-901);
}

static bool lifetime_stopping,lifetime_starting,lifetime_seen;
static void (*lifetime_old_observe)(struct gf3_host *,const struct gf3_host_event *);
static void lifetime_observe(struct gf3_host *h,const struct gf3_host_event *e)
{
    if (lifetime_stopping || lifetime_starting) {
        assert(!cint_prod.feature5.plan_guard.valid);
        lifetime_seen=true;
    }
    if (lifetime_old_observe) lifetime_old_observe(h,e);
}

void _start(void)
{
    scanlog_map(CINT_FB_PHYS,GF_MODE_VRAM_BYTES);
    scanlog_map(SCANLOG_BAR0,0x101000U);
    scanlog_init();
    struct gf5 *g=&cint_prod.feature5;
    assert(g->heat.epoch==g->ch->epoch && g->heat.epoch);
    assert(!g->heat.down_active && !g->heat.critical_paused && !g->heat.stop_pending);
    lifetime_seed(g);
    lifetime_old_observe=cint_host.observe;
    cint_host.observe=lifetime_observe;
    lifetime_stopping=true;
    /* Admission failure still invalidates before hardware/cleanup and keeps
     * the protection state. A retry must finish the same ownership cleanup. */
    svc_close_rc=SVC_INJECT;
    assert(gf4_service_stop(&svc_ctx,cint_deadline())==SVC_INJECT);
    assert(!g->plan_guard.valid && !g->accepting && !svc_ctx.accepting);
    lifetime_preserved(g);
    svc_close_rc=0;
    assert(!gf4_service_stop(&svc_ctx,cint_deadline()));
    assert(lifetime_seen);
    lifetime_stopping=false;
    u32 old_epoch=g->heat.epoch;
    lifetime_preserved(g);
    lifetime_seed(g);
    lifetime_starting=true; lifetime_seen=false;
    assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    lifetime_starting=false;
    assert(lifetime_seen && !g->plan_guard.valid);
    assert(g->ch->epoch!=old_epoch && g->heat.epoch==g->ch->epoch);
    assert(!g->heat.critical_paused && !g->heat.cool_samples);
    lifetime_preserved(g);
    assert(g->accepting && svc_ctx.accepting);
    cint_host.observe=lifetime_old_observe;
    scanlog_finish();
    gf2_host_exit(0);
}
