/* C2-service, UNIT 14: real F1/F2/F3 service and V0, no libc.
 * Reuse C1's PCI/IRQ/DMA/push consumer, replacing its display-only observer
 * and owner callbacks (which assume F3-owned scan nodes). Link real F4
 * service/recover/save/scan, B0-B12/C0 and data helpers; do not link a second
 * copy of TUs included by gf_accel_c1_host.c. A2 owns the link registration.
 * No lifecycle API is stubbed. Test callbacks model the external owner and
 * slot boundary with real F3 admission. Expected ordering is independently
 * nouveau c03c1a1 nouveau_drm.c:955-988; retained mode/IRQ is X3/N1-N3. */
#define GF3_CINT_C2
#include "gf_accel_c1_host.c"
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"

#ifndef GF4_SERVICE_SOURCE
#define GF4_SERVICE_SOURCE "../../hwdev/gf/display/service.c"
#endif
#include GF4_SERVICE_SOURCE

static struct gf4_host svc_host;
static struct gf4_state svc_state;
static struct gf4_host_event svc_events[GF4_HEADS*GF4_HOST_LUT_BYTES*16U];
static struct gf3_host_reg svc_regs[GF4_HEADS*GF4_HOST_VGA_REGS];
static struct gf4_ctx svc_ctx;
static struct gf4_links svc_links;
static struct gf4_exec_ops svc_exec;
static const struct gf4_test_ops *svc_slot;
static i32 svc_open_rc,svc_close_rc,svc_restore_rc,svc_acquire_rc;
static u32 svc_opens,svc_closes,svc_restores,svc_acquires,svc_releases;
static u32 svc_cleanup_after_off;
/* Arbitrary injected negative values, deliberately outside F1-F4 enums. */
enum { SVC_INJECT=-1701, SVC_OTHER=-1702 };

static bool svc_display_reg(u32 off)
{
    /* nvreg.h:60,82,129-153: VGA aliases plus PCRTC/PRAMDAC.
     * F2 registers still use C1's real reference consumer. */
    return off==GF_PLL_GATE || off>=0x600000U ||
           (off>=0x0c0000U && off<0x0c4000U);
}
static int svc_read(void *owner,u32 bar,u32 off,u32 width,u32 *out)
{
    assert(owner==&cint_host);
    return svc_display_reg(off) ? gf4_host_read(&svc_host,bar,off,width,out) :
                                 gf3_host_read(owner,bar,off,width,out);
}
static int svc_write(void *owner,u32 bar,u32 off,u32 width,u32 value)
{
    assert(owner==&cint_host);
    return svc_display_reg(off) ? gf4_host_write(&svc_host,bar,off,width,value) :
                                 gf3_host_write(owner,bar,off,width,value);
}
static void svc_observe(struct gf3_host *h,const struct gf3_host_event *e)
{
    if (!e->rc && e->op==H_WRITE && e->a[1]==GF2_USER_BASE+GF2_USER_PUT)
        cint_consume(e->a[3]);
    if (!e->rc && e->op==H_WRITE) {
        /* W1C source addresses from irq.c:106-125,172-227. */
        const u32 status[]={GF_IRQ_FIFO_STATUS,GF_IRQ_GRAPH_STATUS,GF_IRQ_CRTC_STATUS,
            GF_IRQ_CRTC_STATUS+GF_IRQ_HEAD_STRIDE,0x008100U,0x009100U,0x001100U,0x00b100U};
        for (u32 i=0;i<sizeof(status)/sizeof(status[0]);++i)
            if (e->a[1]==status[i]) gf3_host_set(h,0,status[i],0);
    }
    if (svc_slot) assert(svc_ctx.ready && svc_ctx.accepting && cint_prod.accel.ready);
}
static i32 svc_admission(void *owner,bool open)
{
    struct gf4_ctx *c=owner;
    assert(c==&svc_ctx);
    if (!open) {
        ++svc_closes; svc_slot=0;
        i32 rc=gf3_service_admission(cint_service,false);
        return rc ? rc : svc_close_rc;
    }
    ++svc_opens;
    assert(c->ready && c->epoch==c->ch->epoch && c->ch->state==GF2_RUN);
    if (svc_open_rc) return svc_open_rc;
    i32 rc=gf3_service_admission(cint_service,true);
    if (!rc) {
        /* Borrow a non-NULL token only; no invented gf4 ops are invoked. */
        c->accepting=true;
        svc_slot=(const struct gf4_test_ops *)&svc_ctx;
    }
    return rc;
}
static i32 svc_i2c(void *owner,u8 index,struct gf4_i2c_port *out)
{
    struct gf4_ctx *c=owner;
    if (index>=sizeof(c->dcb.i2c_rc)/sizeof(c->dcb.i2c_rc[0])) return GF4_ENODEV;
    if (c->dcb.i2c_rc[index]) return c->dcb.i2c_rc[index];
    *out=c->dcb.i2c[index]; return 0;
}
static i32 svc_changed(void *owner,u8 head,const struct gf3_surface *front)
{
    assert(owner==&svc_ctx);
    if (head) return 0;
    return gf3_service_surfaces(cint_service,front,
                               cint_service->test.bytes ? &cint_service->test : 0);
}
static i32 svc_failed(void *owner,i32 cause,u32 dl)
{
    assert(owner==&svc_ctx);
    svc_ctx.recovery_rc=gf4_service_stop(owner,dl);
    return cause;
}
static int svc_foundation_start(struct gf_display_context *l)
{
    assert(l==&cint_prod.display);
    return gf_foundation_start(&cint_prod.foundation);
}
static int svc_foundation_stop(struct gf_display_context *l)
{
    assert(l==&cint_prod.display && !svc_slot);
    ++svc_cleanup_after_off;
    assert(cint_prod.chan.state==GF2_OFF || cint_prod.foundation.revoked);
    return gf_foundation_stop(&cint_prod.foundation);
}
static int svc_validate(struct gf_display_context *l)
{
    assert(l==&cint_prod.display); return 0;
}
static int svc_acquire(struct gf_display_context *l)
{
    assert(l==&cint_prod.display); ++svc_acquires;
    return svc_acquire_rc;
}
static void svc_release(struct gf_display_context *l)
{
    assert(l==&cint_prod.display); ++svc_releases;
}
static int svc_restore(void *owner,struct gf_init_context *cleanup)
{
    assert(owner==&cint_host && cleanup->io.opaque==owner && !svc_slot);
    assert(cint_prod.chan.state==GF2_OFF && !cint_prod.accel.ready);
    ++svc_restores;
    return svc_restore_rc;
}
static const struct gf_display_owner svc_display_owner={
    .acquire=svc_acquire,.release=svc_release,.validate=svc_validate,
    .foundation_start=svc_foundation_start,.foundation_stop=svc_foundation_stop,
    .command_get=cint_command_get
};
static const struct gf4_data_ops svc_data={GF4_DATA_VERSION,sizeof(svc_data),
    gf4_edid_modes,gf4_tv_state_calc,gf4_tv_modes};

static void svc_fixture(void)
{
    cint_reset(); /* real F1/F2 RUN, no F3 objects/reservations yet. */
    gf2_host_memset(&svc_state,0,sizeof(svc_state));
    gf2_host_memset(&svc_ctx,0,sizeof(svc_ctx));
    gf4_host_init(&svc_host,&svc_state);
    gf3_host_current=&cint_host;
    svc_host.events=svc_events; svc_host.cevents=sizeof(svc_events)/sizeof(svc_events[0]);
    svc_host.base.regs=svc_regs; svc_host.base.cregs=sizeof(svc_regs)/sizeof(svc_regs[0]);
    cint_host.observe=svc_observe;
    cint_prod.foundation.io.read=svc_read; cint_prod.foundation.io.write=svc_write;
    cint_prod.display.io=cint_prod.foundation.io;
    cint_prod.display.io.restore=svc_restore;
    cint_prod.display.owner=&svc_display_owner;
    cint_prod.display.engaged=cint_prod.display.active=false;
    cint_prod.display.initialized=true; cint_prod.display.baseline_valid=true;
    svc_ctx.ch=&cint_prod.chan; svc_ctx.accel=&cint_prod.accel;
    svc_ctx.state=&svc_state; svc_ctx.data=&svc_data; svc_ctx.exec=&svc_exec;
    svc_state.version=GF4_STATE_VERSION; svc_state.size=sizeof(svc_state);
    /* Independent synthetic ROM limits used by gfinit_mode_host.c:61-64. */
    for (u8 h=0;h<GF4_HEADS;++h) {
        svc_ctx.limits[h]=(struct gfinit_pll_limits){
            {100000,400000,1000,27000,1,255,1,27},
            {400000,1000000,25000,200000,4,40,1,4},6,0,27000};
        svc_ctx.limits_valid[h]=true;
    }
    /* dispnv04/disp.c:247-294, dcb.c:121-193: internal analog A, OR A.
     * No DDC port deliberately selects the §3 VGA fallback. */
    svc_ctx.dcb.nroutes=1;
    svc_ctx.dcb.routes[0]=(struct gf4_route){.type=GF4_ANALOG,.head_mask=1,
        .or_mask=1,.i2c_index=U8_MAX};
    svc_ctx.routes=svc_ctx.dcb.routes; svc_ctx.nroutes=svc_ctx.dcb.nroutes;
    svc_links=(struct gf4_links){svc_i2c,svc_admission,svc_failed,svc_changed,&svc_ctx};
    svc_slot=0;
    svc_open_rc=svc_close_rc=svc_restore_rc=svc_acquire_rc=0;
    svc_opens=svc_closes=svc_restores=svc_acquires=svc_releases=svc_cleanup_after_off=0;
    assert(!gf4_service_bind(&svc_ctx,&cint_prod.display,&svc_links));
    assert(!gf3_service_owner_bind(cint_service,gf4_service_owner_table()));
}
static void svc_keys_live(const struct gf3_surface *surface)
{
    struct gf_vram_reservation key;
    assert(!gf_vram_match(&cint_prod.chan,0,surface->reservation_id,
        surface->generation,surface->off,surface->bytes,&key));
}
static void svc_finish(void)
{
    svc_restore_rc=svc_open_rc=svc_close_rc=0;
    cint_unmap_fail=-1;
    gf3_host_trace_clear(&cint_host);
    gf2_host_memset(cint_host.fail_rc,0,sizeof(cint_host.fail_rc));
    gf4_host_trace_clear(&svc_host);
    gf2_host_memset(svc_host.fail_rc,0,sizeof(svc_host.fail_rc));
    (void)gf4_service_stop(&svc_ctx,cint_deadline());
    assert(!svc_slot && !svc_ctx.ready && !cint_prod.display.engaged);
    assert(cint_prod.display.display4==&svc_ctx && svc_ctx.legacy==&cint_prod.display);
    assert(cint_service->owner==gf4_service_owner_table());
    cint_no_resources();
}
static void test_bind_and_start(void)
{
    svc_fixture();
    svc_ctx.tv_sense_valid=true;
    assert(gf4_service_bind(0,&cint_prod.display,&svc_links)==GF4_EINVAL);
    struct gf_display_context other={0};
    assert(gf4_service_bind(&svc_ctx,&other,&svc_links)==GF4_ESTATE);
    assert(!other.display4 && cint_prod.display.display4==&svc_ctx);
    assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    assert(svc_ctx.accepting && svc_ctx.ready && svc_slot && svc_opens==1);
    assert(cint_prod.accel.ready && cint_slot && svc_state.head[0].enabled);
    assert(svc_state.head[0].mode.fmt==GF3_INDEX8);
    assert(svc_state.head[0].mode.t.hd==GF_MODE_WIDTH &&
           svc_state.head[0].mode.t.vd==GF_MODE_HEIGHT);
    assert(!svc_ctx.tv_sense_valid && !svc_ctx.saved_closed);
    assert(cint_slot->test_surface->pitch==GF_MODE_WIDTH &&
        cint_slot->test_surface->height==GF_MODE_HEIGHT &&
        cint_slot->test_surface->bytes==GF_MODE_WIDTH*GF_MODE_HEIGHT);
    svc_keys_live(&svc_state.head[0].front);
    assert(gf4_service_start(&svc_ctx,cint_deadline())==GF4_ESTATE);
    svc_finish();
    /* N1: both reciprocal loans and the table address survive full stop. */
    const struct gf3_service_owner *table=gf4_service_owner_table();
    u32 stopped_epoch=svc_ctx.ch->epoch;
    assert(table->start(table->owner,cint_deadline())==GF4_ESTATE);
    assert(svc_ctx.ch->state==GF2_OFF && svc_ctx.ch->epoch==stopped_epoch);
    assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    assert(table==gf4_service_owner_table() && svc_slot && svc_ctx.accepting);
    svc_finish();
}
static void test_cleanup_failures(void)
{
    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    struct gf3_surface old=svc_state.head[0].front;
    svc_restore_rc=SVC_INJECT;
    assert(gf4_service_stop(&svc_ctx,cint_deadline())==SVC_INJECT);
    assert(!svc_slot && cint_prod.display.engaged && cint_prod.chan.state==GF2_OFF);
    svc_keys_live(&old);
    assert(cint_prod.display.display4==&svc_ctx && !svc_ctx.ready);
    svc_finish();

    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    old=svc_state.head[0].front;
    cint_open_windows(); cint_unmap_fail=CINT_MAP_VIEW0;
    u32 before=svc_cleanup_after_off;
    assert(gf4_service_stop(&svc_ctx,cint_deadline())==GF2_EMAP);
    assert(cint_prod.chan.state==GF2_RUN && cint_prod.accel.ready);
    assert(svc_cleanup_after_off==before && cint_view[0].mapped && !svc_slot);
    svc_keys_live(&old); svc_finish();

    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    assert(!svc_admission(&svc_ctx,false)); svc_ctx.accepting=false;
    ++cint_prod.foundation.stats.fifo;
    assert(gf2_fault_check(&cint_prod.chan)==GF2_EFAULT);
    /* OFF plus old fault is not a successful stop, but permits cleanup. */
    assert(gf4_service_stop(&svc_ctx,cint_deadline())==GF2_EFAULT);
    assert(cint_prod.chan.state==GF2_OFF && !cint_prod.display.engaged && !registry.head);
    svc_finish();

    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    old=svc_state.head[0].front;
    assert(!svc_admission(&svc_ctx,false)); svc_ctx.accepting=false;
    ++cint_prod.foundation.stats.graph;
    assert(gf2_fault_check(&cint_prod.chan)==GF2_EFAULT);
    gf3_host_trace_clear(&cint_host);
    gf3_host_fail_at(&cint_host,H_WRITE,1,SVC_INJECT);
    assert(gf4_service_stop(&svc_ctx,cint_deadline())==GF2_ESTOP);
    assert(cint_prod.chan.state!=GF2_OFF && cint_prod.accel.ready && !svc_slot);
    svc_keys_live(&old); svc_finish();

    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    /* Saved physical blank skips DPMS settling. The first lower clock is
     * therefore real F3 fini's fence clock; later F2 clocks still succeed. */
    svc_ctx.saved_closed=true;
    gf3_host_trace_clear(&cint_host);
    gf3_host_fail_at(&cint_host,H_CLOCK,1,SVC_INJECT);
    svc_restore_rc=SVC_OTHER;
    u32 dl=cint_deadline();
    assert(gf4_service_stop(&svc_ctx,dl)==GF2_EIO);
    assert(svc_ctx.deadline==dl && svc_ctx.last_rc==GF2_EIO);
    assert(cint_prod.chan.state==GF2_OFF && svc_restores && !svc_slot);
    svc_keys_live(&svc_state.head[0].front); svc_finish();
}
static void test_diagnostic_roundtrip(void)
{
    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    const struct gf3_service_owner *table=gf4_service_owner_table();
    assert(gf3_service_start(cint_service,cint_deadline())==GF3_ESTATE);
    assert(gf3_service_stop(cint_service)==GF3_ESTATE);
    /* N3 normal contrast: retain a non-ON request before suspend sets ON. */
    assert(!gf4_dpms_set(&svc_ctx,0,GF4_STANDBY,cint_deadline()));
    struct gf3_surface front=svc_state.head[0].front;
    u32 id=svc_state.head[0].mode.id,epoch=svc_ctx.epoch;
    u32 heads=cint_prod.foundation.wanted_heads;
    assert(!table->stop(table->owner,cint_deadline()));
    assert(!svc_slot && !svc_ctx.ready && cint_prod.chan.state==GF2_OFF);
    assert(svc_ctx.hold_saved.head[0].dpms==GF4_STANDBY);
    assert(svc_ctx.hold_saved.wanted_heads==heads);
    svc_keys_live(&front);
    assert(!table->start(table->owner,cint_deadline()));
    assert(svc_ctx.epoch!=epoch && svc_slot && svc_ctx.accepting);
    assert(svc_state.head[0].mode.id==id && svc_state.head[0].dpms==GF4_STANDBY);
    assert(!gf2_host_memcmp(&svc_state.head[0].front,&front,sizeof(front)));
    svc_keys_live(&front);
    assert(table->start(table->owner,cint_deadline())==GF4_ESTATE);
    /* An F5-owned closed save must stay closed across diagnostic restart. */
    svc_ctx.saved_closed=true;
    assert(!table->stop(table->owner,cint_deadline()));
    assert(!table->start(table->owner,cint_deadline()));
    assert(!svc_slot && !svc_ctx.accepting && svc_ctx.saved_closed);
    svc_finish();
}
static void test_failed_start(void)
{
    svc_fixture(); svc_open_rc=SVC_INJECT;
    assert(gf4_service_start(&svc_ctx,cint_deadline())==SVC_INJECT);
    assert(!svc_slot && !svc_ctx.accepting && !registry.head);
    assert(svc_ctx.last_rc==SVC_INJECT && cint_prod.display.last_rc==SVC_INJECT);
    svc_finish();
    svc_fixture(); cint_prod.display.initialized=false;
    svc_acquire_rc=SVC_OTHER;
    assert(gf4_service_start(&svc_ctx,cint_deadline())==SVC_OTHER);
    assert(svc_acquires==1 && svc_releases==1 && !svc_opens);
    assert(!cint_prod.display.init && !cint_prod.display.program && !svc_slot);
    svc_finish();
}
void _start(void)
{
    test_bind_and_start(); test_cleanup_failures();
    test_diagnostic_roundtrip(); test_failed_start();
    gf2_host_exit(0);
}
