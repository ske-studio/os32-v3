/* Layer 5 foreground service and diagnostic dispatch.
 * GF_FEAT5_DESIGN, final C2-service and phase/slot contracts.  This is
 * OS32 lifecycle glue; it does not implement a hardware sequence. */
#include "production.h"
#include "../../include/kprintf.h"

static struct gf5_test_ops service_ops;
static const struct gf5_test_ops **service_slot;
static struct gf5 *service_ctx;

/* nouveau c03c1a1 therm/base.c:343-357 and clk/base.c:591-598:
 * workers end before their device lifetime ends. OS32 also validates borrowed
 * diagnostic ops at each call, since a caller may retain them after stop. */
static i32 service_ready(struct gf5 *g)
{
    if (!g) return GF5_EINVAL;
    if (g!=service_ctx) return GF5_ESTATE;
    if (g->phase!=GF5_RUN || g->heat.critical_paused || !g->accepting || !g->ch || !g->ch->f ||
        g->ch->state!=GF2_RUN || g->ch->fault.latched || !g->ch->f->running ||
        !g->display || g->display->ch!=g->ch ||
        !g->display->ready || !g->display->accepting || g->display->changing ||
        g->display->saved_closed || !g->ch->epoch ||
        g->display->epoch!=g->ch->epoch)
        return GF5_ESTATE;
    return 0;
}

static i32 service_live(struct gf5 *g)
{
    if (!g) return GF5_EINVAL;
    if (g!=service_ctx) return GF5_ESTATE;
    if (g->busy || (service_slot && !*service_slot)) return GF5_EBUSY;
    return service_ready(g);
}

static i32 service_check(struct gf5 *g, bool management)
{
    i32 rc;
    if (!g) return GF5_EINVAL;
    if (g!=service_ctx) return GF5_ESTATE;
    if (management) {
        rc=(g->busy || (service_slot && !*service_slot)) ? GF5_EBUSY :
           (g->phase==GF5_RUN || g->phase==GF5_SAVED || g->phase==GF5_FAULT) ?
           0 : GF5_ESTATE;
    } else rc=service_live(g);
    if (rc) {
        if (rc==GF5_ESTATE) gf5_plan_invalidate(g);
        g->last_rc=rc;
    }
    return rc;
}

static void service_complete(struct gf5 *g)
{
    g->busy=false;
    if (g->phase==GF5_FAULT) gf5_plan_invalidate(g);
}

/* Sensor setup normally happens on the first RUN poll. Once initialized,
 * monitoring needs the MMIO lifetime and epoch, never drawing health. */
static bool thermal_live(struct gf5 *g)
{
    if (!g || g!=service_ctx || (service_slot && !*service_slot) ||
        !g->ch || !g->ch->f ||
        g->ch->f->revoked || !g->ch->f->bar0 || !g->display ||
        g->display->ch!=g->ch || !g->display->legacy ||
        !g->display->legacy->bar0_map || !g->ch->epoch ||
        g->display->epoch!=g->ch->epoch)
        return false;
    return g->fan.sensor_setup || !service_live(g);
}

static void service_heat_failure(struct gf5 *g,i32 rc)
{
    /* Preserve the failing boundary even if closing admission also fails. */
    (void)g->links.admission(g->links.owner,false);
    g->accepting=false;
    g->phase=GF5_FAULT;
    gf5_plan_invalidate(g);
    g->source_rc=rc;
    g->last_rc=g->heat.last_rc=gf5_rc_map(rc);
    g->heat.cool_samples=0;
    g->heat.stop_pending=true;
    g->poll_source="critical";
}

/* MIT nouveau c03c1a1 therm/temp.c:104-158 supplies the edge callback.
 * Guard design section 6 maps it to the existing OS32 drain/pause links;
 * no display/scanout programming or snapshot state belongs here. */
static void service_heat_pause(void *owner,bool active)
{
    struct gf5 *g=owner;
    if (!thermal_live(g)) return;
    i32 rc;
    if (active) {
        if (g->heat.critical_paused) return;
        /* A saved/faulted or already closed channel has another owner.
         * Never acquire its pause, or reopen it on the falling edge. */
        bool live=!service_ready(g);
        rc=g->links.admission(g->links.owner,false);
        g->accepting=false;
        if (rc) { service_heat_failure(g,rc); return; }
        if (!live) return;
        u64 now;
        rc=g->links.clock64(g->links.owner,&now);
        if (!rc) {
            g->deadline=(u32)now+GF2_TIMEOUT_US;
            rc=g->links.drain_pause(g->links.owner);
        }
        if (rc) { service_heat_failure(g,rc); return; }
        g->heat.epoch=g->ch->epoch;
        g->heat.critical_paused=true;
        g->heat.cool_samples=0;
        gf5_plan_invalidate(g);
        return;
    }
    if (!g->heat.critical_paused || g->heat.epoch!=g->ch->epoch) return;
    /* Closed admission is ours; all other reasons for closure still win.
     * Do not resume a stopped FIFO or one now owned by save/restore. */
    if (g->phase!=GF5_RUN || g->heat.stop_pending ||
        g->ch->state!=GF2_RUN || g->ch->fault.latched || g->ch->f->running==false ||
        !g->display->ready || g->display->changing || g->display->saved_closed)
        return;
    rc=g->links.unpause(g->links.owner);
    if (rc) { service_heat_failure(g,rc); return; }
    g->heat.critical_paused=false;
    g->heat.cool_samples=0;
    rc=g->links.admission(g->links.owner,true);
    if (rc) service_heat_failure(g,rc);
}

#define SERVICE_OP(name, args, call) \
    static i32 service_##name args { \
        i32 rc=service_check(g,false); \
        if (rc) return rc; \
        g->busy=true; \
        rc=gf5_##name call; \
        service_complete(g); \
        return rc; \
    }
SERVICE_OP(clock_read,(struct gf5 *g,struct gf5_clock *v),(g,v))
SERVICE_OP(clock_calc,(struct gf5 *g,const struct gf5_clock *v,struct gf5_plan *p),(g,v,p))
SERVICE_OP(clock_prog,(struct gf5 *g,const struct gf5_plan *p),(g,p))
SERVICE_OP(ram_prog,(struct gf5 *g,const struct gf5_plan *p),(g,p))
SERVICE_OP(perf_count,(struct gf5 *g,u32 *v),(g,v))
SERVICE_OP(perf_get,(struct gf5 *g,u32 i,struct gf5_perf_entry *v),(g,i,v))
SERVICE_OP(perf_select,(struct gf5 *g,u8 i),(g,i))
SERVICE_OP(therm_read,(struct gf5 *g,struct gf5_thermal *v),(g,v))
SERVICE_OP(fan_mode_set,(struct gf5 *g,enum gf5_fan_mode m),(g,m))
SERVICE_OP(fan_get,(struct gf5 *g,u32 *v),(g,v))
SERVICE_OP(fan_cstate,(struct gf5 *g,u8 p,i32 d),(g,p,d))
SERVICE_OP(volt_get,(struct gf5 *g,u32 *v),(g,v))
SERVICE_OP(volt_set,(struct gf5 *g,u32 v),(g,v))
SERVICE_OP(volt_id,(struct gf5 *g,u8 id,u8 min_id,i32 condition),(g,id,min_id,condition))
SERVICE_OP(gpio_find,(struct gf5 *g,u8 tag,u8 *line,u8 log[2]),(g,tag,line,log))
SERVICE_OP(gpio_get,(struct gf5 *g,u8 line,const u8 log[2],bool *v),(g,line,log,v))
SERVICE_OP(gpio_set,(struct gf5 *g,u8 line,const u8 log[2],bool v),(g,line,log,v))
SERVICE_OP(tile_find,(struct gf5 *g,u32 *slot),(g,slot))
SERVICE_OP(tile_set,(struct gf5 *g,u32 slot,const struct gf5_tile *v),(g,slot,v))
SERVICE_OP(tile_clear,(struct gf5 *g,u32 slot),(g,slot))
#undef SERVICE_OP

static void service_fan_tick(struct gf5 *g,u64 now)
{
    if (!thermal_live(g) || g->busy || g->poll_off) return;
    g->busy=true;
    gf5_fan_tick(g,now);
    service_complete(g);
}

i32 gf5_service_poll(struct gf5 *g)
{
    u64 now;
    i32 rc, previous_rc, previous_source_rc;
    const char *previous_source;
    if (!g) return GF5_EINVAL;
    if (g->busy || g->poll_off || !thermal_live(g)) return 0;
    g->busy=true;
    rc=g->links.clock64 ? g->links.clock64(g->links.owner,&now) : GF5_EINVAL;
    if (rc) {
        g->source_rc=rc;
        g->last_rc=gf5_rc_map(rc);
        g->poll_source="clock64";
        g->poll_off=true;
        kprintf(KLOG_ATTR,"[gf5] clock off rc=%d reason=clock64\n",rc);
        service_complete(g);
        return 0; /* clock failure is confined to the F5 worker. */
    }
    /* A successful tick must not erase an earlier diagnostic. */
    previous_source=g->poll_source;
    previous_rc=g->last_rc;
    previous_source_rc=g->source_rc;
    g->last_rc=0;
    gf5_fan_tick(g,now);
    rc=g->last_rc;
    if (!rc) {
        g->last_rc=previous_rc;
        g->source_rc=previous_source_rc;
        g->poll_source=previous_source;
    }
    service_complete(g);
    return 0; /* F5 alarms never fail foreground rendering. */
}

static i32 service_poll(struct gf5 *g)
{
    return gf5_service_poll(g);
}

static void service_publish(struct gf5 *g)
{
    if (service_slot)
        *service_slot=(g->phase==GF5_SAVING || g->phase==GF5_RESTORING) ?
                      0 : &service_ops;
}

static i32 service_state_save(struct gf5 *g,struct gf5_image **out)
{
    i32 rc=service_check(g,false);
    if (rc) return rc;
    gf5_plan_invalidate(g);
    *service_slot=0;
    rc=gf5_state_save(g,out);
    if (g->phase==GF5_FAULT) gf5_plan_invalidate(g);
    service_publish(g);
    return rc;
}

static i32 service_state_restore(struct gf5 *g,const struct gf5_image *image)
{
    i32 rc=service_check(g,true);
    if (rc) return rc;
    gf5_plan_invalidate(g);
    *service_slot=0;
    rc=gf5_state_restore(g,image);
    if (g->phase==GF5_FAULT) gf5_plan_invalidate(g);
    service_publish(g);
    return rc;
}

static void service_state_free(struct gf5 *g,struct gf5_image *image)
{
    if (!service_check(g,true)) {
        g->busy=true;
        gf5_state_free(g,image);
        service_complete(g);
    }
}

static i32 service_last_rc(struct gf5 *g)
{
    return g && g==service_ctx ? g->last_rc : GF5_EINVAL;
}

i32 gf5_service_init(struct gf_production *p,struct gf4_ctx *display,
                     const struct gf5_test_ops **slot)
{
    struct gf5_links *links;
    struct gf5 *g;
    if (!p || !display || !slot) return GF5_EINVAL;
    if (p->display.display4!=display || display->legacy!=&p->display ||
        display->ch!=&p->chan || display->accel!=&p->accel) return GF5_ESTATE;
    g=&p->feature5;
    links=gf5_links_table();
    if (!links || !links->drain_pause || !links->unpause ||
        !links->display_save || !links->display_restore || !links->memory_script ||
        !links->admission || !links->drain || !links->stop || !links->restart ||
        !links->copy_extent || !links->alloc || !links->free || !links->clock64)
        return GF5_EINVAL;
    if ((service_ctx && service_ctx!=g) || (service_slot && service_slot!=slot) ||
        (links->owner && links->owner!=g)) return GF5_ESTATE;
    if (service_ctx) return 0;
    g->ch=&p->chan; g->accel=&p->accel; g->display=display;
    links->owner=g;
    g->links=*links;
    g->emergency.pause=service_heat_pause;
    g->emergency.owner=g;
    service_ctx=g; service_slot=slot;
    service_ops=(struct gf5_test_ops){
        .ctx=g,.clock_read=service_clock_read,.clock_calc=service_clock_calc,
        .clock_prog=service_clock_prog,.ram_prog=service_ram_prog,
        .perf_count=service_perf_count,.perf_get=service_perf_get,
        .perf_select=service_perf_select,.therm_read=service_therm_read,
        .fan_mode_set=service_fan_mode_set,.fan_get=service_fan_get,
        .fan_cstate=service_fan_cstate,.fan_tick=service_fan_tick,
        .volt_get=service_volt_get,.volt_set=service_volt_set,.volt_id=service_volt_id,
        .gpio_find=service_gpio_find,.gpio_get=service_gpio_get,.gpio_set=service_gpio_set,
        .tile_find=service_tile_find,.tile_set=service_tile_set,.tile_clear=service_tile_clear,
        .state_save=service_state_save,.state_restore=service_state_restore,
        .state_free=service_state_free,.service_poll=service_poll,.last_rc=service_last_rc
    };
    service_publish(g);
    return 0;
}
