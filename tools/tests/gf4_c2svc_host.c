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

static u32 stop_order[8],stop_tick;
#ifndef GF_STARTLOG_HOST_H
static bool stop_fonts_logged;
void __cdecl kprintf(u8 attr,const char *fmt,...)
{
    const char *expected="[gf4] stop fonts skipped diagnostic_saved\n";
    (void)attr;
    while (*expected && *expected==*fmt) { ++expected; ++fmt; }
    if (!*expected && !*fmt) stop_fonts_logged=true;
}
#endif
static void svc_mark(u32 step) { if (!stop_order[step]) stop_order[step]=++stop_tick; }
#define gf3_service_close(s) (svc_mark(3),gf3_service_close(s))
#define gf3_service_fini(s,dl) (svc_mark(4),gf3_service_fini(s,dl))
#define gf2_stop(ch) (svc_mark(6),gf2_stop(ch))
static i32 __attribute__((unused)) svc_detect_budget(struct gf4_ctx *,u32);
#ifndef GF4_SERVICE_SOURCE
#define GF4_SERVICE_SOURCE "../../hwdev/gf/display/service.c"
#define gf4_detect svc_detect_budget
#endif
#include GF4_SERVICE_SOURCE
#undef gf4_detect
#undef gf3_service_close
#undef gf3_service_fini
#undef gf2_stop
/* Include the real module service so a fixture reboot can forget its empty
 * publication binding before invoking the production initializer again. */
#define service_check guard_f5_check
#define service_publish guard_f5_publish
#include "../../hwdev/gf/feature5_service.c"
#undef service_publish
#undef service_check
static const struct gf5_test_ops *guard_f5_slot;
static struct gf5_stop_state guard_stop;

static const struct gf4_test_ops *svc_slot;
static struct gf4_state svc_state;
#ifdef GF4_GUARD_LIFETIME
/* H2 uses the actual stopped management slot published by service.c. */
static const struct gf4_restore_ops *guard_management;
static GfxBackend guard_backend;
static struct geforce_platform guard_platform={.display_phys=CINT_FB_PHYS};
static int guard_height;
static GeforceModuleContext guard_module={.display_restore_out=&guard_management,
    .display_out=&svc_slot,.backend=&guard_backend,.platform=&guard_platform,
    .height=&guard_height,.display_state=&svc_state};
const GeforceModuleContext *geforce_context=&guard_module;
#endif
static struct gf4_host svc_host;
static struct gf4_host_event svc_events[GF4_HEADS*GF4_HOST_LUT_BYTES*16U];
static struct gf3_host_reg svc_regs[GF4_HEADS*GF4_HOST_VGA_REGS];
static struct gf4_ctx svc_ctx;
static struct gf4_links svc_links;
static struct gf4_exec_ops svc_exec;
static i32 svc_open_rc,svc_close_rc,svc_restore_rc,svc_acquire_rc;
static u32 svc_opens,svc_closes,svc_restores,svc_acquires,svc_releases;
static u32 svc_cleanup_after_off;
static bool guard_retire_fail;
static bool svc_slow_validate,svc_spend_objects,svc_spend_detect;
static i32 svc_detect_budget(struct gf4_ctx *c,u32 dl)
{
    i32 rc=gf4_detect(c,dl);
    if (!rc && svc_spend_detect) cint_host.now=dl;
    return rc;
}
/* Arbitrary injected negative values, deliberately outside F1-F4 enums. */
enum { SVC_INJECT=-1701, SVC_OTHER=-1702 };

static bool svc_display_reg(u32 off)
{
    /* nvreg.h:60,82,129-153: VGA aliases plus PCRTC/PRAMDAC.
     * F2 registers still use C1's real reference consumer. */
    return off==GF_PLL_GATE || (off>=0x600000U && off<GF2_USER_BASE) ||
           (off>=0x0c0000U && off<0x0c4000U);
}
static int svc_read(void *owner,u32 bar,u32 off,u32 width,u32 *out)
{
    assert(owner==&cint_host);
    /* The font plane window is separate from the F2 register oracle. The
     * focused hwstate fixture checks plane contents and selection. */
    if (bar==GF_MODE_VRAM_BAR) { assert(width==4 && off<65536U); *out=0; return 0; }
    return svc_display_reg(off) ? gf4_host_read(&svc_host,bar,off,width,out) :
                                 gf3_host_read(owner,bar,off,width,out);
}
static int svc_write(void *owner,u32 bar,u32 off,u32 width,u32 value)
{
    assert(owner==&cint_host);
    if (bar==GF_MODE_VRAM_BAR) { svc_mark(5); return 0; }
    if (off==0x68052cU) svc_mark(value==1U ? 1 : 0);
    if (off==0x6013d5U && svc_host.index[0][0]==0x44U) svc_mark(2);
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
    if (svc_spend_objects && !e->rc && e->op==H_READ &&
        e->a[1]==GF2_USER_BASE+GF2_USER_REFERENCE &&
        cint_prod.chan.state==GF2_RUN && cint_prod.accel.notify_storage.size) {
        cint_host.now=svc_ctx.deadline; svc_spend_objects=false;
    }
    if (svc_slot) assert(svc_ctx.ready && svc_ctx.accepting && cint_prod.accel.ready);
}
static i32 svc_admission(void *owner,bool open)
{
    struct gf4_ctx *c=owner;
    assert(c==&svc_ctx);
    if (!open) {
        ++svc_closes; svc_slot=0;
#ifdef GF4_GUARD_LIFETIME
        i32 rc=gf4_links_table()->admission(c,false);
#else
        i32 rc=gf3_service_admission(cint_service,false);
#endif
        return rc ? rc : svc_close_rc;
    }
    ++svc_opens;
    assert(c->ready && c->epoch==c->ch->epoch && c->ch->state==GF2_RUN);
    if (svc_open_rc) return svc_open_rc;
#ifdef GF4_GUARD_LIFETIME
    return gf4_links_table()->admission(c,true);
#else
    i32 rc=gf3_service_admission(cint_service,true);
    if (!rc) {
        c->accepting=true;
        svc_slot=(const struct gf4_test_ops *)&svc_ctx;
    }
    return rc;
#endif
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
    assert(l==&cint_prod.display);
    if (svc_slow_validate) cint_host.now+=GF2_TIMEOUT_US;
    return 0;
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
    if (guard_retire_fail)
        gf3_host_fail_at(&cint_host,H_WRITE,cint_host.calls[H_WRITE]+4U,SVC_INJECT);
    /* Three PC98 quiesce writes precede tile retirement. */
    return svc_restore_rc;
}
static const struct gf_display_owner svc_display_owner={
    .acquire=svc_acquire,.release=svc_release,.validate=svc_validate,
    .foundation_start=svc_foundation_start,.foundation_stop=svc_foundation_stop,
    .command_get=cint_command_get
};
static const struct gf4_data_ops svc_data={GF4_DATA_VERSION,sizeof(svc_data),
    gf4_edid_modes,gf4_tv_state_calc,gf4_tv_modes};

static void svc_fixture_state(bool cold)
{
    gf4_hw_state_reset();
    if (cold) cint_reset_cold();
    else cint_reset(); /* real F1/F2 RUN, no F3 objects/reservations yet. */
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
    svc_slot=0; svc_slow_validate=svc_spend_objects=svc_spend_detect=false;
    svc_open_rc=svc_close_rc=svc_restore_rc=svc_acquire_rc=0;
    svc_opens=svc_closes=svc_restores=svc_acquires=svc_releases=svc_cleanup_after_off=0;
    assert(!gf4_service_bind(&svc_ctx,&cint_prod.display,&svc_links));
    assert(!gf3_service_owner_bind(cint_service,gf4_service_owner_table()));
    /* cint_reset simulates reboot and clears production storage. No live
     * loans survive it; recreate F5 through the same init as module startup. */
    service_ctx=NULL; service_slot=NULL;
    guard_stop=(struct gf5_stop_state){0};
    cint_prod.feature5.stop=&guard_stop;
    assert(!gf5_service_init(&cint_prod,&svc_ctx,&guard_f5_slot));
}
static void svc_fixture(void)
{
    svc_fixture_state(false);
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
    /* The test owns the simulated F5 closed-save loan. End that loan
     * before requesting complete teardown, as the real F5 owner does. */
    svc_ctx.saved_closed=false;
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
    assert(cint_prod.feature5.accepting);
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
    assert(!cint_prod.feature5.accepting);
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
    /* Stop has no DPMS ON settle. With no valid hardware snapshot,
     * the first lower clock is real F3 fini; later F2 clocks succeed. */
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
    /* N3: retain a non-ON request; suspend does not write DPMS (S4b). */
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
static void test_stopped_cursor(void)
{
    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    /* Simulate recovery having stopped the channel while cursor HW stays on. */
    assert(!svc_admission(&svc_ctx,false));
    svc_state.head[0].cursor_valid=true;
    assert(!gf4_cursor_apply(&svc_ctx,0,true));
    assert(!gf2_stop(svc_ctx.ch));
    svc_ctx.ready=false;
    svc_finish();
    assert(!(svc_host.cr[0][0x31] & 1U));
}
static void test_slow_start(void)
{
    svc_fixture(); svc_slow_validate=svc_spend_objects=svc_spend_detect=true;
    u32 dl=cint_deadline();
    assert(!gf4_service_start(&svc_ctx,dl));
    assert(svc_ctx.accepting && svc_state.head[0].enabled);
    assert((i32)(svc_ctx.deadline-dl)>=(i32)(3U*GF2_TIMEOUT_US));
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
#ifdef GF4_GUARD_LIFETIME
/* Guard lifetime A8/A9: nouveau c03c1a1 dispnv04/disp.c:189-207
 * ends saved-image ownership after display use. Fallible OS32 cleanup must
 * retain real registry nodes until a retry, including after saved_release.
 * MM/production adapter atomicity is exercised by H1/H4, not modeled here. */
static struct gf4_saved guard_saved,guard_stale;
static struct gf_vram_borrow guard_loan;
static u32 guard_reservations(void)
{
    u32 cursor=0,count=0;
    struct gf_vram_reservation key;
    int rc;
    while ((rc=gf_vram_next(&cint_prod.chan,&cursor,&key))>0) ++count;
    assert(!rc);
    return count;
}
static void guard_pending(void)
{
    assert(svc_ctx.cleanup_pending && !svc_ctx.accepting && !svc_slot);
    assert(cint_prod.display.display4==&svc_ctx && svc_ctx.legacy==&cint_prod.display);
    assert(cint_service->owner==gf4_service_owner_table());
    assert(guard_management && guard_management->ctx==&svc_ctx);
    assert(guard_management->abandon && guard_management->saved_release);
    u32 epoch=cint_prod.chan.epoch,opens=svc_opens;
    assert(gf4_service_start(&svc_ctx,cint_deadline())==GF4_EBUSY);
    assert(cint_prod.chan.epoch==epoch && svc_opens==opens);
}
static void guard_clean(void)
{
    assert(!svc_ctx.cleanup_pending && !svc_ctx.diagnostic_saved && !svc_ctx.saved_closed);
    assert(!guard_management && !registry.borrows && !guard_reservations());
    assert(!svc_ctx.hold_saved.version && !cint_service->scan.bytes && !cint_service->test.bytes);
    for (u8 h=0;h<GF4_HEADS;++h) {
        struct gf4_head_image zero={0};
        struct gf4_flip_hold hold={0};
        assert(!gf2_host_memcmp(&svc_state.head[h],&zero,sizeof(zero)));
        assert(!gf2_host_memcmp(&svc_ctx.hold[h],&hold,sizeof(hold)));
        assert(!svc_ctx.scan_bank[h] && !svc_ctx.cursor_view[h].mapped);
    }
    for (u32 i=0;i<GF5_TILE_COUNT;++i) {
        assert(cint_prod.feature5.tile_slot[i].state==GF_TILE_FREE);
        assert(!cint_prod.feature5.tile_live[i]);
    }
    cint_no_resources();
}
static void guard_reenter(void)
{
    guard_clean();
    /* No fixture reset: new reservations/epoch must be produced by enter. */
    assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    assert(svc_ctx.ready && svc_ctx.accepting && svc_slot);
    svc_keys_live(&svc_state.head[0].front);
    assert(gf4_diagnostic_abandon(&svc_ctx,&guard_saved)==GF4_ESTATE);
    svc_finish(); guard_clean();
}
static void test_terminal_guard(void)
{
    /* Successful restore is the normal contrast to terminal abandonment. */
    svc_fixture_state(true);
    assert(!cint_prod.chan.f);
    assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    assert(svc_slot && svc_slot->display_save);
    assert(!svc_slot->display_save(&svc_ctx,&guard_saved,cint_deadline()));
    assert(guard_management && guard_management->restore);
    assert(!guard_management->restore(&svc_ctx,&guard_saved,cint_deadline()));
    assert(guard_management && guard_management->saved_release);
    assert(!guard_management->saved_release(&svc_ctx,&guard_saved));
    svc_finish(); guard_reenter();

    /* quiesce failure must keep engaged and refuse abandon, even with OFF. */
    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    assert(svc_slot && svc_slot->display_save);
    assert(!svc_slot->display_save(&svc_ctx,&guard_saved,cint_deadline()));
    gf4_host_fail_at(&svc_host,D_WRITE,svc_host.calls[D_WRITE]+1U,SVC_INJECT);
    assert(guard_management && guard_management->restore);
    assert(guard_management->restore(&svc_ctx,&guard_saved,cint_deadline())==SVC_INJECT);
    u32 count=guard_reservations();
    svc_restore_rc=SVC_OTHER;
    assert(gf4_service_stop(&svc_ctx,cint_deadline())==SVC_OTHER);
    assert(cint_prod.chan.state==GF2_OFF && cint_prod.display.engaged);
    guard_pending();
    assert(guard_management->abandon(&svc_ctx,&guard_saved)==GF4_EBUSY);
    assert(guard_reservations()==count && svc_ctx.diagnostic_saved==&guard_saved);
    svc_restore_rc=0;
    assert(!gf4_service_stop(&svc_ctx,cint_deadline()));
    guard_pending();
    assert(guard_management->abandon(&svc_ctx,&guard_stale)==GF4_ESTATE);
    svc_ctx.last_rc=SVC_INJECT;
    assert(!guard_management->abandon(&svc_ctx,&guard_saved));
    assert(svc_ctx.last_rc==SVC_INJECT);
    assert(gf4_diagnostic_abandon(&svc_ctx,&guard_saved)==GF4_ESTATE);
    /* Repeated shutdown has no reservation or DMA left to free twice. */
    assert(!gf4_service_stop(&svc_ctx,cint_deadline()));
    guard_clean();
    /* Legacy quiesce failure permanently revokes reuse (quiesce.c), even
     * after terminal memory cleanup. Never reopen unsafe hardware. */
    assert(!cint_prod.display.reusable);
    assert(gf4_service_start(&svc_ctx,cint_deadline())==GF4_ESTATE);

    /* A8's restore-only failure, with successful complete stop, permits
     * reentry on the same initialized owner without a fixture reset. */
    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    assert(svc_slot && svc_slot->display_save);
    assert(!svc_slot->display_save(&svc_ctx,&guard_saved,cint_deadline()));
    gf4_host_fail_at(&svc_host,D_WRITE,svc_host.calls[D_WRITE]+1U,SVC_INJECT);
    assert(guard_management && guard_management->restore);
    assert(guard_management->restore(&svc_ctx,&guard_saved,cint_deadline())==SVC_INJECT);
    assert(!gf4_service_stop(&svc_ctx,cint_deadline()));
    guard_pending();
    assert(!guard_management->abandon(&svc_ctx,&guard_saved));
    guard_reenter();

    /* saved_release succeeds, but a real outstanding loan makes release_all
     * fail. Its key/node must survive without the diagnostic pointer. */
    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    assert(svc_slot && svc_slot->display_save);
    assert(!svc_slot->display_save(&svc_ctx,&guard_saved,cint_deadline()));
    struct gf_vram_reservation key=svc_ctx.test.value;
    assert(!gf_vram_borrow_get(&cint_prod.chan,&guard_loan,&key,key.off,key.bytes,GF_BORROW_VIEW));
    assert(!gf4_service_stop(&svc_ctx,cint_deadline()));
    guard_pending(); count=guard_reservations();
    svc_ctx.last_rc=SVC_INJECT;
    assert(guard_management->abandon(&svc_ctx,&guard_saved)==GF3_EBUSY);
    assert(svc_ctx.last_rc==SVC_INJECT && !svc_ctx.diagnostic_saved);
    assert(guard_reservations()==count);
    assert(!gf2_host_memcmp(&key,&svc_ctx.test.value,sizeof(key)));
    guard_pending();
    assert(!gf_vram_borrow_put(&cint_prod.chan,&guard_loan));
    assert(!guard_management->abandon(&svc_ctx,&guard_saved));
    guard_reenter();
}
static void test_stop_guard(void)
{
    /* A failed window close leaves both its view loan and reservation live. */
    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    cint_open_windows();
    u32 count=guard_reservations();
    struct gf_vram_borrow loan=cint_view[0].borrow;
    cint_unmap_fail=CINT_MAP_VIEW0;
    assert(gf4_service_stop(&svc_ctx,cint_deadline())==GF2_EMAP);
    guard_pending();
    assert(guard_reservations()==count && cint_view[0].mapped);
    assert(!gf2_host_memcmp(&loan,&cint_view[0].borrow,sizeof(loan)));
    svc_finish(); guard_reenter();

    /* A10: set a real tile on the scan reservation, leave without clear.
     * Arm a PFB write failure at the external restore boundary (after OFF),
     * so no earlier F2/F1 write consumes the injected failure. */
    svc_fixture(); assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    struct gf_vram_reservation key=svc_ctx.scan[0][0].value;
    /* nouveau_bo.c:178-188: 64 rows at the NV4x minimum pitch. */
    enum { GUARD_TILE_PITCH=1024U, GUARD_TILE_ROWS=64U };
    struct gf5_tile tile={.base=key.off,.bytes=GUARD_TILE_PITCH*GUARD_TILE_ROWS,.pitch=GUARD_TILE_PITCH,
        .reservation_id=key.reservation_id,.generation=key.generation};
    cint_prod.feature5.deadline=cint_deadline();
    assert(!gf5_tile_set(&cint_prod.feature5,0,&tile));
    assert(cint_prod.feature5.tile_slot[0].state==GF_TILE_OWNED);
    guard_retire_fail=true;
    assert(gf4_service_stop(&svc_ctx,cint_deadline())==GF5_EIO);
    assert(cint_prod.feature5.source_rc==SVC_INJECT);
    guard_retire_fail=false;
    guard_pending();
    assert(!cint_prod.display.engaged && cint_prod.chan.state==GF2_OFF);
    assert(cint_prod.feature5.tile_slot[0].state==GF_TILE_UNKNOWN);
    assert(gf_vram_release(&cint_prod.chan,&key)==GF3_EBUSY);
    svc_finish(); guard_reenter();
}
#endif
static u32 stop_fonts[65536];
static void test_hw_restore(void)
{
    for (u32 variant=0;variant<5;++variant) {
        bool no_f=variant==1,diagnostic=variant==2,valid=variant!=3,fonts=variant!=3 && variant!=4;
        svc_fixture_state(no_f);
        if (!no_f) assert(!gf4_service_start(&svc_ctx,cint_deadline()));
        else cint_prod.display.engaged=true;
        struct gf4_hw_saved *saved=gf4_hw_saved_get();
        saved->valid=valid; saved->nroutes=1; saved->output[0].output=1;
        saved->fonts=stop_fonts; saved->fonts_valid=fonts;
        struct gf4_saved image={0};
        if (diagnostic) svc_ctx.diagnostic_saved=&image;
        for (u32 h=0;h<2;++h) {
            saved->crtc[h].cr[0x1a]=0x3f; saved->crtc[h].cr[0x21]=0xfa;
            svc_host.cr[h][0x17]=0; /* stop must never assert sync */
        }
        gf4_host_trace_clear(&svc_host);
        gf2_host_memset(stop_order,0,sizeof(stop_order)); stop_tick=0;
#ifndef GF_STARTLOG_HOST_H
        stop_fonts_logged=false;
#endif
        if (no_f) svc_ctx.ch->f=0;
        assert(gf4_service_stop(&svc_ctx,cint_deadline())==(no_f ? GF4_EINVAL : 0));
#ifndef GF_STARTLOG_HOST_H
        assert(stop_fonts_logged==diagnostic);
#endif
        if (valid) {
            assert(stop_order[1] && stop_order[2]>stop_order[1]);
            assert(stop_order[3]>stop_order[2] && stop_order[4]>stop_order[3]);
            if (diagnostic || !fonts) assert(!stop_order[5]);
            else assert(stop_order[5]>stop_order[4] && stop_order[6]>stop_order[5]);
            if (!no_f) assert(stop_order[0] && stop_order[1]>stop_order[0]);
            for (u32 h=0;h<2;++h) {
                assert(svc_host.cr[h][0x17]==0 && svc_host.cr[h][0x1a]==0x3f && svc_host.cr[h][0x21]==0xfa);
            }
        } else assert(!stop_order[0] && !stop_order[1] && !stop_order[2]);
        u8 index[2]={0};
        for (u32 i=0;i<svc_host.nevents;++i) {
            const struct gf4_host_event *e=&svc_events[i];
            if (e->op!=D_WRITE) continue;
            for (u32 h=0;h<2;++h) {
                if (e->a[1]==0x6013d4U+h*0x2000) index[h]=e->a[3];
                if (e->a[1]==0x6013d5U+h*0x2000 && index[h]==0x17) assert(!(e->a[3]&0x80));
            }
        }
        saved->fonts=0; saved->fonts_valid=false; svc_ctx.diagnostic_saved=0; svc_finish();
    }
}
void _start(void)
{
    test_hw_restore();
#ifdef GF4_GUARD_LIFETIME
    test_terminal_guard(); test_stop_guard();
#endif
    test_bind_and_start(); test_cleanup_failures();
    test_diagnostic_roundtrip(); test_stopped_cursor(); test_slow_start(); test_failed_start();
    gf2_host_exit(0);
}
