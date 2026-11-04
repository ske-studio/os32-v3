/* Layer 5 foreground service and diagnostic dispatch.
 * GF_FEAT5_DESIGN, final C2-service and phase/slot contracts.  This is
 * OS32 lifecycle glue; it does not implement a hardware sequence. */
#include "production.h"

static struct gf5_test_ops service_ops;
static const struct gf5_test_ops **service_slot;
static struct gf5 *service_ctx;

static i32 service_check(struct gf5 *g, bool management)
{
    if (!g) return GF5_EINVAL;
    if (g!=service_ctx) return GF5_ESTATE;
    if (g->phase==GF5_RUN ||
        (management && (g->phase==GF5_SAVED || g->phase==GF5_FAULT)))
        return 0;
    g->last_rc=GF5_ESTATE;
    return GF5_ESTATE;
}

#define SERVICE_OP(name, args, call) \
    static i32 service_##name args { \
        i32 rc=service_check(g,false); \
        return rc ? rc : gf5_##name call; \
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
    if (!service_check(g,false)) gf5_fan_tick(g,now);
}

i32 gf5_service_poll(struct gf5 *g)
{
    u64 now;
    i32 rc, previous_rc, previous_source_rc;
    if (!g) return GF5_EINVAL;
    if (g->busy || g->phase!=GF5_RUN) return 0;
    if (!g->links.clock64) return g->last_rc=GF5_EINVAL;
    rc=g->links.clock64(g->links.owner,&now);
    if (rc) {
        g->source_rc=rc;
        return g->last_rc=gf5_rc_map(rc);
    }
    /* A successful tick must not erase an earlier diagnostic. */
    previous_rc=g->last_rc;
    previous_source_rc=g->source_rc;
    g->last_rc=0;
    gf5_fan_tick(g,now);
    rc=g->last_rc;
    if (!rc) {
        g->last_rc=previous_rc;
        g->source_rc=previous_source_rc;
    }
    return rc;
}

static i32 service_poll(struct gf5 *g)
{
    i32 rc=service_check(g,false);
    return rc ? rc : gf5_service_poll(g);
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
    *service_slot=0;
    rc=gf5_state_save(g,out);
    service_publish(g);
    return rc;
}

static i32 service_state_restore(struct gf5 *g,const struct gf5_image *image)
{
    i32 rc=service_check(g,true);
    if (rc) return rc;
    *service_slot=0;
    rc=gf5_state_restore(g,image);
    service_publish(g);
    return rc;
}

static void service_state_free(struct gf5 *g,struct gf5_image *image)
{
    if (!service_check(g,true)) gf5_state_free(g,image);
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
