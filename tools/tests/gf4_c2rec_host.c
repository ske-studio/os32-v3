/* C2-recover, UNIT 14: real service/V0/F1/F2 and F4 products.
 * Reuse the existing libc-free hardware fixture, not lifecycle stubs.
 * GF3_CINT_C2 suppresses its main; link real accel_copy.c as for C1.
 * Link recover.c, save.c and their real F4/C0 dependencies with
 * GF_PRODUCTION_MODULE, -m32 -nostdlib -ffunction-sections/--gc-sections.
 * A2 owns registration. No new service API or F4 operation is stubbed.
 * Oracle: MIT nouveau c03c1a1 nouveau_drm.c:944-1027, fifo/uchan.c:272-298;
 * Suspend leaves DPMS registers untouched (nouveau_display_fini).
 * Reservation identity and publication predicates are OS32 X1-X4/N2/N3. */
#define GF3_CINT_C2
#include "gf_accel_c1_host.c"
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#include "../../hwdev/gf/display/hw_state.h"

/* Rebooted fixture storage needs the real F5 binding before stop retires
 * tiles (GF_GUARD_LIFETIME_DESIGN section 4). No thermal policy is faked. */
#define service_check guard_f5_check
#define service_publish guard_f5_publish
#include "../../hwdev/gf/feature5_service.c"
#undef service_publish
#undef service_check
static const struct gf5_test_ops *guard_f5_slot;

/* Typed links callback exported by recover.c; C2-links binds this name. */
i32 gf4_service_failed(void *,i32,u32);
static struct gf4_host host;
static struct gf4_state state;
static struct gf4_saved diagnostic;
static struct gf4_host_event events[CINT_EVENTS];
static struct gf3_host_reg display_regs[1024]; /* VGA/PLL/LUT trace capacity */
static struct gf4_route route;
static struct gf4_head_image original[GF4_HEADS];
static u32 original_fifo,original_graph,original_heads;
static struct gf_vram_reservation keys[4]; /* front, back, cursor, test */
static bool fail_cleanup,fail_close,fail_publish,fail_restore;
static u32 close_calls,publish_calls,deadline,notifications;
static bool expect_fault_stop;
static u32 fault_references;
static bool slow_restart,spend_objects_budget;
static u32 stage_clocks;
static enum { STAGE_NORMAL, OBJECTS_CLOCK_FAIL, OBJECTS_EXPIRE,
              RESTORE_CLOCK_FAIL, RESTORE_EXPIRE, RESTORE_FENCE_EXPIRE } stage_fault;
/* Hardware oracle, not values read from the product. crtc.c:198-233. */
#define T_CLOCK 1U
#define T_MODE 0x17U
#define T_SYNC 0x1aU
#define T_DACCLK 0x68052cU
#define T_DIAGNOSTIC_PATTERN 0xa5U /* caller-owned saved-image sentinel */
/* nvreg.h:129-153,433-437: VGA and display alias range boundaries. */
#define T_DISPLAY_BASE 0x600000U
#define T_VGA_BASE 0x0c0000U
#define T_VGA_END 0x0c4000U
#define T_FAILURE CINT_IO_FAILURE
#define T_CAUSE GF4_EPLL
#define T_PAGE_UP(n) (((n)+MEM_PAGE_SIZE-1U)&~(MEM_PAGE_SIZE-1U))

static bool display_address(u32 off)
{
    /* nvreg.h:129-153,294-303,329-348,433-437: display/VGA ranges.
     * PCRTC IRQ status/enable belong to the real F1 register fixture. */
    if (off==GF_IRQ_CRTC_STATUS || off==GF_IRQ_CRTC_ENABLE ||
        off==GF_IRQ_CRTC_STATUS+GF_IRQ_HEAD_STRIDE ||
        off==GF_IRQ_CRTC_ENABLE+GF_IRQ_HEAD_STRIDE) return false;
    return (off>=T_DISPLAY_BASE && off<GF2_USER_BASE) || (off>=T_VGA_BASE && off<T_VGA_END);
}
static int display_read(void *owner,u32 bar,u32 off,u32 width,u32 *out)
{
    assert(owner==&host);
    return display_address(off) ? gf4_host_read(&host,bar,off,width,out) :
                                 gf3_host_read(&cint_host,bar,off,width,out);
}
static int display_write(void *owner,u32 bar,u32 off,u32 width,u32 value)
{
    assert(owner==&host);
    return display_address(off) ? gf4_host_write(&host,bar,off,width,value) :
                                 gf3_host_write(&cint_host,bar,off,width,value);
}
static int display_local_restore(void *owner,struct gf_init_context *cleanup)
{
    assert(owner==&host && cleanup->io.opaque==owner);
    /* This environment has no adapter-owned VGA locks/windows. Execute
     * actual instruction saves if present; no display snapshot is faked. */
    if (cleanup->saved_count) return gf_init_restore(cleanup);
    assert(cint_prod.chan.state==GF2_OFF);
    ++cint_restore_calls;
    return 0;
}
static int display_clock(void *owner,u32 *now)
{
    assert(owner==&host);
    bool objects=cint_prod.chan.state==GF2_RUN && !cint_prod.accel.ready;
    bool restore=cint_prod.accel.ready && host.ctx.ready;
    if ((objects && (stage_fault==OBJECTS_CLOCK_FAIL || stage_fault==OBJECTS_EXPIRE)) ||
        (restore && (stage_fault==RESTORE_CLOCK_FAIL || stage_fault==RESTORE_EXPIRE ||
                     stage_fault==RESTORE_FENCE_EXPIRE))) {
        ++stage_clocks;
        if (stage_clocks==1 && (stage_fault==OBJECTS_CLOCK_FAIL || stage_fault==RESTORE_CLOCK_FAIL))
            return T_FAILURE;
        if (stage_clocks==(stage_fault==RESTORE_FENCE_EXPIRE ? 3U : 2U))
            host.local_now+=GF2_TIMEOUT_US;
    }
    i32 rc=gf4_host_clock(&host,now);
    if (!rc) cint_host.now=*now;
    return rc;
}
static int display_wait(void *owner,u32 us)
{
    assert(owner==&host);
    i32 rc=gf4_host_wait(&host,us);
    cint_host.now=(u32)host.local_now;
    return rc;
}
static void lower_observe(struct gf3_host *h,const struct gf3_host_event *e)
{
    assert(h==&cint_host);
    if (!e->rc && e->op==H_WRITE && e->a[1]==GF2_USER_BASE+GF2_USER_PUT)
        cint_consume(e->a[3]);
    /* Same W1C device behavior as cint_observe, without its F3-owned-node
     * assertion: X1 deliberately makes the F4 resident nodes the owners. */
    if (!e->rc && e->op==H_WRITE) {
        const u32 status[]={GF_IRQ_FIFO_STATUS,GF_IRQ_GRAPH_STATUS,
            GF_IRQ_CRTC_STATUS,GF_IRQ_CRTC_STATUS+GF_IRQ_HEAD_STRIDE,
            0x008100U,0x009100U,0x001100U,0x00b100U}; /* irq.c: unused_status */
        for (u32 i=0;i<sizeof(status)/sizeof(status[0]);++i)
            if (e->a[1]==status[i]) gf3_host_set(h,0,status[i],0);
    }
    /* Last F3 reference is observed just before init returns. Spending the
     * remaining budget here models time passing before the next stage. */
    if (spend_objects_budget && !e->rc && e->op==H_READ &&
        e->a[1]==GF2_USER_BASE+GF2_USER_REFERENCE &&
        cint_prod.chan.state==GF2_RUN && !host.ctx.ready &&
        !cint_prod.accel.ready && cint_prod.accel.notify_storage.size) {
        host.local_now=host.ctx.deadline; cint_host.now=(u32)host.local_now;
        spend_objects_budget=false;
    }
    if (cint_expect_closed) assert(!cint_slot && !host.ctx.accepting);
}
static i32 admission(void *owner,bool open)
{
    assert(owner==&host);
    if (open) {
        ++publish_calls;
        assert(host.ctx.ready && cint_prod.accel.ready && cint_prod.chan.state==GF2_RUN);
        if (fail_publish) return T_FAILURE;
    }
    /* Use the real new service boundary. No object, close, or surface stub. */
    i32 rc=gf3_service_admission(cint_service,open);
    return rc ? rc : gf4_host_admission(owner,open);
}
static int owner_start(struct gf_display_context *l)
{
    assert(l==&cint_prod.display && !host.ctx.accepting && !cint_slot);
    assert(cint_prod.foundation.bm); /* master before device/object init */
    i32 rc=gf_foundation_start(&cint_prod.foundation);
    /* Long pre-F3 stage (devinit/F1 equivalent), using the real restart. */
    if (!rc && slow_restart) {
        host.local_now+=GF2_TIMEOUT_US; cint_host.now=(u32)host.local_now;
    }
    if (!rc && fail_restore)
        gf4_host_fail_at(&host,D_WRITE,host.calls[D_WRITE]+1U,T_FAILURE);
    return rc;
}
static int owner_stop(struct gf_display_context *l)
{
    assert(l==&cint_prod.display && cint_prod.chan.state==GF2_OFF);
    assert(!cint_slot && !host.ctx.accepting && !cint_prod.accel.ready);
    ++cint_owner_stops;
    if (expect_fault_stop) assert(cint_references==fault_references);
    i32 rc=fail_cleanup ? T_FAILURE : gf_foundation_stop(&cint_prod.foundation);
    /* The legacy owner records the detailed rc for local quiesce too. */
    l->last_rc=rc;
    return rc;
}
static int owner_validate(struct gf_display_context *l)
{
    assert(l==&cint_prod.display && cint_prod.foundation.bm);
    assert(!cint_slot && !host.ctx.accepting);
    return 0;
}
static int owner_acquire(struct gf_display_context *l)
{ (void)l; assert(0); return T_FAILURE; } /* initialized fixture: no devinit */
static void owner_release(struct gf_display_context *l) { (void)l; assert(0); }
static const struct gf_display_owner owner={
    .foundation_start=owner_start,.foundation_stop=owner_stop,
    .validate=owner_validate,.acquire=owner_acquire,.release=owner_release,
    .command_get=cint_command_get
};
void geforce_display_failed(void)
{
    assert(!host.ctx.accepting);
    ++notifications;
}
static i32 window_close(void *owner,struct gf_vram_view *v)
{
    assert(owner==&host && !host.ctx.accepting && !cint_slot);
    ++close_calls;
    if (fail_close) return T_FAILURE;
    return gf_vram_view_unmap(v);
}
static void trace_clear(void)
{
    cint_trace_clear(); gf4_host_trace_clear(&host);
    publish_calls=close_calls=0;
}
static void retained(void)
{
    struct gf_vram_reservation live;
    for (u32 i=0;i<sizeof(keys)/sizeof(keys[0]);++i) {
        const struct gf_vram_reservation *v=&keys[i];
        assert(!gf_vram_match(&cint_prod.chan,v->owner,v->reservation_id,
                              v->generation,v->off,v->bytes,&live));
        assert(!gf2_host_memcmp(v,&live,sizeof(live)));
    }
    assert(host.ctx.legacy==&cint_prod.display && cint_prod.display.display4==&host.ctx);
}
static void recover_setup(enum gf3_fmt fmt)
{
    cint_reset();
    gf4_hw_state_reset();
    gf2_host_memset(&state,0,sizeof(state));
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=sizeof(events)/sizeof(events[0]);
    host.base.regs=display_regs; host.base.cregs=sizeof(display_regs)/sizeof(display_regs[0]);
    host.ctx.ch=&cint_prod.chan; host.ctx.accel=&cint_prod.accel;
    host.ctx.epoch=cint_prod.chan.epoch; host.ctx.legacy=&cint_prod.display;
    host.links.admission=admission;
    cint_prod.display.display4=&host.ctx; cint_prod.display.owner=&owner;
    cint_prod.display.initialized=cint_prod.display.baseline_valid=true;
    host.local_now=cint_host.now;
    cint_prod.foundation.io=(struct gf_io){.opaque=&host,.read=display_read,
        .write=display_write,.timing={display_clock,display_wait},.restore=display_local_restore};
    cint_prod.display.io=cint_prod.foundation.io;
    cint_host.observe=lower_observe;
    fail_cleanup=fail_close=fail_publish=fail_restore=expect_fault_stop=false;
    notifications=0;
    slow_restart=spend_objects_budget=false; stage_fault=STAGE_NORMAL; stage_clocks=0;
    state.version=GF4_STATE_VERSION; state.size=sizeof(state);
    route=(struct gf4_route){.head=0,.head_mask=1,.or_mask=1,.type=GF4_ANALOG};
    host.ctx.routes=&route; host.ctx.nroutes=1;
    struct gf4_head_image *h=&state.head[0];
    h->enabled=true; h->dpms=GF4_SUSPEND;
    h->mode=(struct gf4_mode){.id=1,.fmt=fmt,
        .pitch=GF_MODE_WIDTH*gf3_host_cpp(fmt),
        .t={GF_MODE_CLOCK_KHZ,GF_MODE_WIDTH,GF_MODE_HSYNC_START,GF_MODE_HSYNC_END,
            GF_MODE_HTOTAL,GF_MODE_HEIGHT,GF_MODE_VSYNC_START,GF_MODE_VSYNC_END,
            GF_MODE_VTOTAL,GF4_NEG_H|GF4_NEG_V}};
    h->route=route;
    u32 bytes=T_PAGE_UP(h->mode.pitch*GF_MODE_HEIGHT),off=CINT_WINDOW_OFF;
    assert(!gf_vram_registry_init(&cint_prod.chan));
    for (u32 n=0;n<2U;++n) {
        struct gf_vram_reservation_node *node=&host.ctx.scan[0][n];
        assert(!gf_vram_reserve(&cint_prod.chan,node,off,bytes,node));
        keys[n]=node->value; off+=bytes;
    }
    assert(!gf_vram_reserve(&cint_prod.chan,&host.ctx.cursor[0],off,
                            GF4_CURSOR_BYTES,&host.ctx.cursor[0]));
    keys[2]=host.ctx.cursor[0].value; off+=GF4_CURSOR_BYTES;
    /* X1: fixed INDEX8 test geometry; independent of selected scan format. */
    assert(!gf_vram_reserve(&cint_prod.chan,&host.ctx.test,off,
                            T_PAGE_UP(GF_MODE_WIDTH*GF_MODE_HEIGHT),&host.ctx.test));
    keys[3]=host.ctx.test.value;
    h->front=(struct gf3_surface){.off=keys[0].off,.bytes=keys[0].bytes,
        .pitch=h->mode.pitch,.width=GF_MODE_WIDTH,.height=GF_MODE_HEIGHT,.fmt=fmt,
        .reservation_id=keys[0].reservation_id,.generation=keys[0].generation};
    h->back=h->front; h->back.off=keys[1].off;
    h->back.reservation_id=keys[1].reservation_id; h->back.generation=keys[1].generation;
    h->cursor=keys[2];
    for (u32 c=0;c<GF4_HOST_RGB;++c) for (u32 i=0;i<GF4_HOST_VGA_REGS;++i)
        h->gamma[c][i]=(u16)(i*(U16_MAX/U8_MAX));
    /* Existing independent PLL fixture: gfinit_mode_host.c:61-64. */
    host.ctx.limits[0]=(struct gfinit_pll_limits){
        {100000,400000,1000,27000,1,255,1,27},
        {400000,1000000,25000,200000,4,40,1,4},6,0,27000};
    host.ctx.limits_valid[0]=true;
    deadline=GF3_TIMEOUT_US; host.ctx.deadline=deadline;
    /* Complete stop is now owned by real C2-service. Borrow its real owner
     * table as well as the F3 service, so final teardown is a valid call. */
    host.ctx.ready=false; host.ctx.legacy=0; cint_prod.display.display4=0;
    assert(!gf4_service_bind(&host.ctx,&cint_prod.display,&host.links));
    service_ctx=0; service_slot=0;
    assert(!gf5_service_init(&cint_prod,&host.ctx,&guard_f5_slot));
    assert(!gf3_service_owner_bind(cint_service,gf4_service_owner_table()));
    assert(!gf3_service_objects_init(cint_service,deadline));
    host.ctx.ready=true;
    struct gf3_surface test={.off=keys[3].off,.bytes=keys[3].bytes,
        .pitch=GF_MODE_WIDTH,.width=GF_MODE_WIDTH,.height=GF_MODE_HEIGHT,.fmt=GF3_INDEX8,
        .reservation_id=keys[3].reservation_id,.generation=keys[3].generation};
    assert(!gf3_service_surfaces(cint_service,&h->front,&test));
    for (u8 i=0;i<GF4_HEADS;++i) original[i]=state.head[i];
    original_fifo=cint_prod.foundation.wanted_fifo;
    original_graph=cint_prod.foundation.wanted_graph;
    original_heads=cint_prod.foundation.wanted_heads;
    /* Keep setup closed: RUN publication is itself under test below. */
    assert(!admission(&host,false));
    trace_clear();
}
static void finish(void)
{
    fail_cleanup=fail_close=fail_publish=fail_restore=expect_fault_stop=false;
    stage_fault=STAGE_NORMAL; slow_restart=spend_objects_budget=false;
    host.ctx.diagnostic_saved=NULL; host.ctx.saved_closed=false;
    deadline=(u32)host.local_now+GF3_TIMEOUT_US; /* distinct cleanup operation */
    cint_expect_closed=true;
    /* Normal stop, not suspend, is the only path that may return nodes. */
    i32 stop_rc=gf4_service_stop(&host.ctx,deadline);
    assert(!stop_rc || stop_rc==GF2_EFAULT);
    assert(cint_prod.chan.state==GF2_OFF && !registry.head && !views);
    cint_no_resources();
}
static u32 dacclk(u32 head)
{
    u32 value=0,off=T_DACCLK+head*GF4_HOST_STRIDE;
    for (u32 b=0;b<4U;++b) value|=gf4_host_reg_byte(&host,0,off+b)<<(b*8U);
    return value;
}
static void test_suspend_restart(void)
{
    const enum gf3_fmt formats[]={GF3_INDEX8,GF3_RGB565,GF3_XRGB8888};
    for (u32 f=0;f<sizeof(formats)/sizeof(formats[0]);++f) {
        recover_setup(formats[f]);
        u32 epoch=cint_prod.chan.epoch;
        host.ctx.saved_closed=true;
        u8 sr[GF4_HEADS],mode[GF4_HEADS],sync[GF4_HEADS];
        u32 dac[GF4_HEADS];
        u8 cr_index[GF4_HEADS],sr_index[GF4_HEADS];
        for (u32 h=0;h<GF4_HEADS;++h) {
            sr[h]=host.sr[h][T_CLOCK]; mode[h]=host.cr[h][T_MODE];
            sync[h]=host.cr[h][T_SYNC];
            dac[h]=dacclk(h);
            cr_index[h]=host.index[h][0]; sr_index[h]=host.index[h][1];
        }
        assert(!gf4_service_suspend(&host.ctx,deadline));
        assert(!cint_prod.accel.ready && !host.ctx.ready && cint_prod.chan.state==GF2_OFF);
        assert(host.ctx.hold_saved.version==GF4_SAVED_VERSION);
        assert(host.ctx.hold_saved.size==sizeof(host.ctx.hold_saved));
        assert(host.ctx.hold_saved.wanted_fifo==original_fifo);
        assert(host.ctx.hold_saved.wanted_graph==original_graph);
        assert(host.ctx.hold_saved.wanted_heads==original_heads);
        assert(!gf2_host_memcmp(original,host.ctx.hold_saved.head,sizeof(original)));
        for (u32 h=0;h<GF4_HEADS;++h) {
            assert(host.sr[h][T_CLOCK]==sr[h] && host.cr[h][T_MODE]==mode[h]);
            assert(host.cr[h][T_SYNC]==sync[h]);
            assert(dacclk(h)==dac[h]);
        }
        for (u32 i=0;i<host.nevents;++i) {
            const struct gf4_host_event *e=&host.events[i];
            if (e->op!=D_WRITE) continue;
            for (u32 h=0;h<GF4_HEADS;++h) {
                u32 d=h*GF4_HOST_STRIDE;
                assert(e->a[1]!=T_DACCLK+d);
                if (e->a[1]==GF4_HOST_SR+d) sr_index[h]=(u8)e->a[3];
                if (e->a[1]==GF4_HOST_CR+d) cr_index[h]=(u8)e->a[3];
                if (e->a[1]==GF4_HOST_SR+d+1U) assert(sr_index[h]!=T_CLOCK);
                if (e->a[1]==GF4_HOST_CR+d+1U)
                    assert(cr_index[h]!=T_MODE && cr_index[h]!=T_SYNC);
            }
        }
        assert(!host.calls[D_SLEEP] && !publish_calls);
        assert(cint_owner_stops==1 && !cint_restore_calls);
        retained();
        assert(!gf4_service_restart(&host.ctx,deadline));
        assert(!gf4_hw_saved_get()->valid);
        assert(cint_prod.chan.epoch==epoch+1U && host.ctx.epoch==cint_prod.chan.epoch);
        assert(cint_prod.accel.ready && host.ctx.ready && !host.ctx.accepting && !cint_slot);
        assert(host.ctx.saved_closed && !publish_calls);
        assert(!gf2_host_memcmp(&cint_service->scan,&original[0].front,sizeof(cint_service->scan)));
        assert(cint_service->test.fmt==GF3_INDEX8 && cint_service->test.pitch==GF_MODE_WIDTH);
        retained(); finish();
    }
}
static void test_close_and_stop_failures(void)
{
    recover_setup(GF3_INDEX8);
    struct gf_vram_view *v=&host.ctx.cursor_view[0];
    v->close=window_close; v->close_owner=&host;
    volatile u8 *va=NULL;
    assert(!gf_vram_view_map(v,&cint_prod.chan,keys[0].off,MEM_PAGE_SIZE,&va));
    fail_close=true;
    assert(gf4_service_suspend(&host.ctx,deadline)==T_FAILURE);
    assert(close_calls==1 && v->mapped && cint_prod.chan.state==GF2_RUN);
    assert(cint_prod.accel.ready && !cint_owner_stops && !host.ctx.accepting && !cint_slot);
    retained(); fail_close=false;
    assert(!gf4_service_suspend(&host.ctx,deadline));
    assert(!v->mapped && close_calls==2); retained(); finish();

    recover_setup(GF3_INDEX8);
    /* Real F2 stop cannot release when the push unmap fails. */
    cint_unmap_fail=CINT_MAP_PUSH;
    assert(gf4_service_suspend(&host.ctx,deadline)==GF2_ESTOP);
    assert(cint_prod.chan.state!=GF2_OFF && !cint_owner_stops && host.ctx.ready);
    retained(); cint_unmap_fail=-1; finish();

    recover_setup(GF3_INDEX8); fail_cleanup=true;
    ++cint_prod.foundation.stats.fifo;
    assert(gf2_fault_check(&cint_prod.chan)==GF2_EFAULT);
    assert(gf4_service_recover(&host.ctx,deadline)==GF2_EFAULT);
    assert(cint_prod.chan.state==GF2_OFF && !host.ctx.ready && !publish_calls);
    retained(); finish();
}
static void test_fault_recover(void)
{
    for (u32 saved=0;saved<3U;++saved) {
        recover_setup(GF3_XRGB8888);
        host.ctx.saved_closed=saved!=0;
        host.ctx.diagnostic_saved=saved==2 ? &diagnostic : NULL;
        gf2_host_memset(&diagnostic,T_DIAGNOSTIC_PATTERN,sizeof(diagnostic)); /* distinct saved-image sentinel */
        struct gf4_saved untouched=diagnostic;
        host.ctx.hold[0]=(struct gf4_flip_hold){.pending=true,.base_written=true,
            .old_front=state.head[0].front,.new_front=state.head[0].back,.after=U32_MAX};
        ++cint_prod.foundation.stats.fifo;
        assert(gf2_fault_check(&cint_prod.chan)==GF2_EFAULT);
        u32 refs=cint_references,epoch=cint_prod.chan.epoch;
        expect_fault_stop=true; fault_references=refs;
        assert(!gf4_service_recover(&host.ctx,deadline));
        /* F2 restart has its own initial reference; no old fault fence,
         * flip wait or fini reference is allowed before OFF. */
        assert(cint_references>refs && cint_prod.chan.epoch==epoch+1U);
        assert(host.ctx.hold_saved.hold[0].pending && !host.ctx.hold[0].pending);
        assert(host.ctx.recovery_rc==GF2_EFAULT && host.ctx.last_rc==0);
        assert(host.ctx.saved_closed==(saved!=0));
        assert(host.ctx.diagnostic_saved==(saved==2 ? &diagnostic : NULL));
        assert(!gf2_host_memcmp(&diagnostic,&untouched,sizeof(diagnostic)));
        assert(host.ctx.accepting==(saved!=1));
        assert(publish_calls==(saved==1 ? 0U : 1U));
        assert(!gf2_host_memcmp(&state.head[0].front,&original[0].front,sizeof(original[0].front)));
        assert((i32)(host.ctx.deadline-deadline)>=0);
        retained(); finish();
    }
}
static void test_restore_and_publication_failure(void)
{
    recover_setup(GF3_INDEX8);
    gf4_host_fail_at(&host,D_WRITE,1,T_FAILURE);
    assert(gf4_service_suspend(&host.ctx,deadline)==T_FAILURE);
    assert(host.ctx.last_rc==T_FAILURE && cint_prod.chan.state==GF2_OFF && !publish_calls);
    retained(); finish();

    recover_setup(GF3_INDEX8); fail_restore=true;
    assert(gf4_service_recover(&host.ctx,deadline)==T_FAILURE);
    assert(host.ctx.ready && cint_prod.accel.ready && !host.ctx.accepting && !cint_slot);
    assert(host.ctx.recovery_rc==T_FAILURE && !publish_calls && host.ctx.saved_closed);
    retained(); finish();

    recover_setup(GF3_RGB565); fail_publish=true;
    assert(gf4_service_recover(&host.ctx,deadline)==T_FAILURE);
    assert(host.ctx.ready && cint_prod.accel.ready && !host.ctx.accepting && !cint_slot);
    assert(host.ctx.recovery_rc==T_FAILURE && publish_calls==1U);
    retained(); finish();
}
static void test_restart_failure(void)
{
    recover_setup(GF3_RGB565);
    assert(!gf4_service_suspend(&host.ctx,deadline));
    u32 epoch=cint_prod.chan.epoch;
    cint_map_fail=CINT_MAP_PUSH;
    assert(gf4_service_restart(&host.ctx,deadline)<0);
    assert(!host.ctx.ready && !host.ctx.accepting && !cint_slot && !publish_calls);
    assert(cint_prod.chan.epoch==epoch+1U);
    retained(); cint_map_fail=-1; finish();

    recover_setup(GF3_INDEX8);
    assert(!gf4_service_suspend(&host.ctx,deadline));
    ++host.ctx.hold_saved.head[0].front.generation;
    assert(gf4_service_restart(&host.ctx,deadline)==GF3_ESTATE);
    assert(!host.ctx.ready && cint_prod.accel.ready && !cint_slot && !publish_calls);
    retained(); finish();

    recover_setup(GF3_INDEX8);
    assert(!gf4_service_suspend(&host.ctx,deadline));
    /* An expired entry deadline does not consume the new F3 stage. */
    host.local_now=deadline; cint_host.now=deadline;
    assert(!gf4_service_restart(&host.ctx,deadline));
    assert(host.ctx.deadline==deadline+GF2_TIMEOUT_US && host.ctx.ready && !cint_slot && !publish_calls);
    retained(); finish();
}
static void test_restart_revoked(void)
{
    recover_setup(GF3_INDEX8);
    assert(!gf4_service_suspend(&host.ctx,deadline));
    gf_foundation_revoke(&cint_prod.foundation);
    trace_clear();
    u32 pci_writes=cint_pci_writes,epoch=cint_prod.chan.epoch;
    u16 command=cint_pci_command;
    /* OS32 lease oracle: foundation.c:101 rejects with GF1_EPCI before
     * master.c:16-17 can write Command. Compare counts, not fixed totals. */
    assert(gf4_service_restart(&host.ctx,deadline)==GF1_EPCI);
    assert(host.ctx.last_rc==GF1_EPCI && cint_prod.foundation.revoked);
    assert(cint_pci_writes==pci_writes && cint_pci_command==command);
    assert(!host.nevents && !cint_host.nevents);
    assert(cint_prod.chan.epoch==epoch && cint_prod.chan.state==GF2_OFF);
    assert(!host.ctx.ready && !cint_prod.accel.ready && !cint_prod.foundation.bm);
    assert(!host.ctx.accepting && !cint_slot && !publish_calls);
    retained();
    /* Revocation ends this fixture's usable lease. Normal video stop must
     * retain its nodes when hardware cleanup cannot be confirmed. Keep this
     * terminal case last instead of resetting live ownership for another. */
    assert(gf4_service_stop(&host.ctx,deadline)==GF1_EPCI);
    assert(cint_prod.chan.state==GF2_OFF && !views && !cint_slot);
    assert(!host.ctx.ready && !host.ctx.accepting);
    retained();
}
static void test_stage_deadlines(void)
{
    /* Both recovery and the actual diagnostic owner table compose restart
     * and restore. F1 consumes the entry budget; F3 consumes its own. */
    for (u32 diagnostic_start=0;diagnostic_start<2;++diagnostic_start) {
        recover_setup(GF3_INDEX8);
        slow_restart=spend_objects_budget=true;
        i32 rc;
        if (diagnostic_start) {
            const struct gf3_service_owner *o=gf4_service_owner_table();
            assert(!o->stop(o->owner,deadline));
            rc=o->start(o->owner,deadline);
        } else rc=gf4_service_recover(&host.ctx,deadline);
        assert(!rc && !spend_objects_budget);
        assert(host.ctx.ready && host.ctx.accepting && cint_prod.accel.ready);
        assert((i32)(host.ctx.deadline-deadline)>=(i32)(2U*GF2_TIMEOUT_US));
        assert((i32)(host.ctx.deadline-(u32)host.local_now)>0);
        retained(); finish();
    }
    const i32 expected[]={GF2_EIO,GF2_ETIME,GF2_EIO,GF4_ETIME,GF2_ETIME};
    for (u32 fault=OBJECTS_CLOCK_FAIL;fault<=RESTORE_FENCE_EXPIRE;++fault) {
        recover_setup(GF3_INDEX8);
        assert(!gf4_service_suspend(&host.ctx,deadline));
        stage_fault=fault; stage_clocks=0;
        const struct gf3_service_owner *o=gf4_service_owner_table();
        assert(o->start(o->owner,deadline)==expected[fault-1U]);
        assert(!host.ctx.accepting && !publish_calls);
        retained(); finish();
    }
}
static void test_failed_and_arguments(void)
{
    assert(gf4_service_suspend(NULL,deadline)==GF4_EINVAL);
    assert(gf4_service_restart(NULL,deadline)==GF4_EINVAL);
    assert(gf4_service_recover(NULL,deadline)==GF4_EINVAL);
    recover_setup(GF3_INDEX8); host.ctx.changing=true;
    assert(gf4_service_suspend(&host.ctx,deadline)==GF4_EBUSY);
    assert(gf4_service_recover(&host.ctx,deadline)==GF4_EBUSY);
    assert(gf4_service_restart(&host.ctx,deadline)==GF4_EBUSY);
    assert(!host.nevents); host.ctx.changing=false;
    fail_cleanup=true;
    assert(gf4_service_failed(&host.ctx,T_CAUSE,deadline)==T_CAUSE);
    assert(host.ctx.last_rc==T_CAUSE && host.ctx.recovery_rc==T_FAILURE);
    assert(!host.ctx.accepting && !host.ctx.reusable && notifications==1U);
    retained(); finish();
}
void _start(void)
{
    test_stage_deadlines();
    test_suspend_restart();
    test_close_and_stop_failures();
    test_fault_recover();
    test_restart_failure();
    test_restore_and_publication_failure();
    test_failed_and_arguments();
    test_restart_revoked();
    gf2_host_exit(0);
}
