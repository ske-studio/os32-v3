/* C2-service-stop, GF4 UNIT14: real F1/F2/F3/V0 and legacy display stop.
 * Reuse C1's integration hardware oracle, not fake service dependencies.
 * Link B4 and F4 service dependencies separately, as for gf_accel_c1_host.c.
 * F4 service.c itself is included below; never link a second copy.
 * PM must first integrate C2-service-f3. No libc; entry _start.
 * Ordering oracle: MIT nouveau c03c1a1 nouveau_drm.c:944-988 (display,
 * idle/fence, objects). Local quiesce values are the PC-98 coexistence
 * contract, hwdev/gf/quiesce_rows.h, not a nouveau register sequence. */
#include "../../include/io.h"
#define irq_save() 0U
#define irq_restore(flags) ((void)(flags))
#ifndef GF_PRODUCTION_MODULE
#define GF_PRODUCTION_MODULE
#endif
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#define GF3_CINT_C2
#include "gf_accel_c1_host.c"
#ifndef GF4_COMMAND_TEST_SOURCE
#define GF4_COMMAND_TEST_SOURCE "../../hwdev/gf/command_test.c"
#endif
#include GF4_COMMAND_TEST_SOURCE
/* UNIT14 links real service dependencies; no gf4_service_stop stub.
 * This TU becomes available when PM integrates C2-service. */
#ifndef GF4_SERVICE_SOURCE
#define GF4_SERVICE_SOURCE "../../hwdev/gf/display/service.c"
#endif
#include GF4_SERVICE_SOURCE

/* Rebooted fixture storage needs the real F5 binding before stop retires
 * tiles (GF_GUARD_LIFETIME_DESIGN section 4). No thermal policy is faked. */
#define service_check guard_f5_check
#define service_publish guard_f5_publish
#include "../../hwdev/gf/feature5_service.c"
#undef service_publish
#undef service_check
static const struct gf5_test_ops *guard_f5_slot;

/* Callback failure is an existing owner boundary, not a fake F3 service
 * symbol. Distinct errors check that neither F4 nor F2 codes are rounded. */
static u32 owner_calls[3], owner_deadline;
static i32 owner_result;
static i32 owner_call(void *owner,u32 dl,u32 op)
{
    assert(owner==cint_service && dl==owner_deadline);
    ++owner_calls[op];
    return owner_result;
}
static i32 owner_start(void *owner,u32 dl) { return owner_call(owner,dl,0); }
static i32 owner_stop(void *owner,u32 dl) { return owner_call(owner,dl,1); }
static i32 owner_recover(void *owner,u32 dl) { return owner_call(owner,dl,2); }
static struct gf3_service_owner test_owner;
static void production_dispatch(void)
{
    /* Dispatch is exercised on the initialized F3 lower owner. Its scan
     * stays pinned until local restoration and the explicit release. */
    cint_reset(); cint_start();
    test_owner=(struct gf3_service_owner){cint_service,owner_start,owner_stop,owner_recover};
    assert(!gf3_service_owner_bind(cint_service,&test_owner));
    gf2_test_bind_production(&cint_prod.foundation,&cint_prod.chan);
    const enum gf2_test_op ops[]={GF2T_START,GF2T_STOP,GF2T_RECOVER};
    const i32 errors[]={GF4_ESTATE,GF2_ETIME,GF3_ESTATE};
    owner_deadline=cint_deadline();
    struct gf2_test_reply reply;
    struct gf2_chan before=cint_prod.chan;
    for (u32 i=0;i<sizeof(ops)/sizeof(ops[0]);++i) {
        struct gf2_test_request q={sizeof(q),ops[i],owner_deadline};
        owner_result=0;
        assert(!gf2_module_ops.call(&cint_prod.foundation,&q,&reply));
        assert(owner_calls[i]==1);
        owner_result=errors[i];
        assert(gf2_module_ops.call(&cint_prod.foundation,&q,&reply)==errors[i]);
        assert(reply.rc==errors[i] && owner_calls[i]==2);
        assert(cint_prod.chan.state==before.state && cint_prod.chan.epoch==before.epoch);
        assert(cint_prod.chan.va==before.va);
    }
    /* Revocation is checked before all three callbacks, even with owner. */
    cint_prod.foundation.revoked=true;
    for (u32 i=0;i<sizeof(ops)/sizeof(ops[0]);++i) {
        struct gf2_test_request q={sizeof(q),ops[i],owner_deadline};
        assert(gf2_module_ops.call(&cint_prod.foundation,&q,&reply)==GF2_EFAULT);
        assert(owner_calls[i]==2);
    }
    cint_prod.foundation.revoked=false;
    /* Owner is borrowed until complete shutdown; no test erases live nodes. */
    assert(!gf3_service_close(cint_service));
    assert(!gf2_stop(&cint_prod.chan));
    gf3_service_invalidate(cint_service);
    /* The owner loan closes after real display cleanup and node release. */
    assert(!gf_display_stop(&cint_prod.display));
    assert(!gf3_service_owner_bind(cint_service,0));
    cint_no_resources();
}
static void quiesce_retains_reservations(void)
{
    cint_reset(); cint_start();
    struct gf_vram_reservation scan=cint_service->scan_node.value;
    struct gf_vram_reservation test=cint_service->test_node.value;
    assert(!gf3_service_stop(cint_service));
    assert(cint_prod.chan.state==GF2_OFF);
    cint_trace_clear();
    assert(!gf_display_quiesce(&cint_prod.display));
    assert(cint_prod.display.engaged && cint_owner_stops==1 && cint_restore_calls==1);
    assert(!gf2_host_memcmp(&scan,&cint_service->scan_node.value,sizeof(scan)));
    assert(!gf2_host_memcmp(&test,&cint_service->test_node.value,sizeof(test)));
    /* Real stop, after the helper, is still responsible for release. */
    cint_restore_seen=false;
    assert(!gf_display_stop(&cint_prod.display));
    cint_no_resources();
}
static void quiesce_first_error(void)
{
    cint_reset(); cint_start();
    assert(!gf3_service_stop(cint_service));
    cint_trace_clear();
    cint_restore_rc=CINT_RESTORE_FAILURE;
    assert(gf_display_quiesce(&cint_prod.display)==CINT_RESTORE_FAILURE);
    assert(cint_prod.display.last_rc==CINT_RESTORE_FAILURE);
    assert(cint_prod.display.engaged && cint_service->scan_node.value.owner);
    assert(cint_restore_calls==1 && cint_owner_stops==1);
    /* All three PC-98 cleanup writes are attempted after local failure. */
    const u32 rows[]={RECOVER_PBUS1850,RECOVER_PBUS1854,GF_PMC_IRQ};
    for (u32 r=0;r<sizeof(rows)/sizeof(rows[0]);++r) {
        bool found=false;
        for (u32 i=0;i<cint_host.nevents;++i) {
            const struct gf3_host_event *e=&cint_events[i];
            if (e->op==H_WRITE && e->a[1]==rows[r] && e->a[3]==0) found=true;
        }
        assert(found);
    }
    cint_restore_rc=0; cint_restore_seen=false;
    assert(!gf_display_stop(&cint_prod.display));
    cint_no_resources();
}
static u32 failing_row, row_write_start;
static int row_failure_restore(void *owner,struct gf_init_context *cleanup)
{
    int rc=cint_restore(owner,cleanup);
    row_write_start=cint_host.calls[H_WRITE];
    gf3_host_fail_at(&cint_host,H_WRITE,row_write_start+failing_row+1U,CINT_IO_FAILURE);
    return rc;
}
static void quiesce_row_failures(void)
{
    const u32 rows[]={RECOVER_PBUS1850,RECOVER_PBUS1854,GF_PMC_IRQ};
    for (u32 r=0;r<sizeof(rows)/sizeof(rows[0]);++r) {
        cint_reset(); cint_start();
        assert(!gf3_service_stop(cint_service));
        cint_trace_clear();
        /* Arm after real foundation cleanup, at the local restore boundary. */
        failing_row=r;
        cint_prod.display.io.restore=row_failure_restore;
        assert(gf_display_quiesce(&cint_prod.display)==CINT_IO_FAILURE);
        assert(cint_prod.display.target==rows[r]);
        assert(cint_prod.display.engaged && cint_service->scan_node.value.owner);
        assert(cint_host.calls[H_WRITE]-row_write_start==sizeof(rows)/sizeof(rows[0]));
        gf3_host_fail_at(&cint_host,H_WRITE,0,0);
        cint_prod.display.io.restore=cint_restore;
        cint_restore_seen=false;
        assert(!gf_display_stop(&cint_prod.display));
        cint_no_resources();
    }
}
static i32 clock_admission(void *owner,bool open)
{
    assert(owner==cint_service && !open);
    return gf3_service_admission(owner,false);
}
static void failed_clock_cleanup(void)
{
    for (u32 missing=0;missing<2;++missing) {
        cint_reset(); cint_start();
        struct gf4_ctx display={.ready=true,.accepting=true,.reusable=true};
        const struct gf4_links links={.owner=cint_service,.admission=clock_admission};
        display.links=&links;
        display.legacy=&cint_prod.display;
        display.dcb.nroutes=2;
        display.dcb.routes[0]=(struct gf4_route){.type=GF4_ANALOG,.or_mask=1};
        display.dcb.routes[1]=(struct gf4_route){.type=GF4_TMDS,.or_mask=2};
        gf4_hw_state_reset();
        struct gf4_hw_saved *saved=gf4_hw_saved_get();
        saved->valid=true; saved->nroutes=2; saved->output[0].output=0x123401;
        saved->crtc[0].fb_start=0x1110; saved->crtc[1].fb_start=0x2220;
        cint_prod.display.display4=&display;
        struct gf_vram_reservation scan=cint_service->scan_node.value;
        cint_trace_clear();
        u32 references=cint_references;
        if (missing) cint_prod.display.io.timing.clock_us=0;
        else gf3_host_fail_at(&cint_host,H_CLOCK,1,CINT_IO_FAILURE);
        assert(gf_display_stop(&cint_prod.display)==-1);
        assert(cint_prod.display.last_rc==GF2_EIO && display.last_rc==GF2_EIO);
        assert(!display.accepting && !display.reusable);
        u32 dacclk; assert(!gf3_host_read(&cint_host,0,0x68052c,4,&dacclk) && dacclk==0x123401);
        /* The no-clock branch loads both CRTCs before lower stop. */
        u32 origins=0, index=0;
        u32 cr1f=0, extended=0;
        for (u32 i=0;i<cint_host.nevents;++i) {
            const struct gf3_host_event *e=&cint_host.events[i];
            if (e->op==H_WRITE && e->a[1]==0x6013d4) index=e->a[3];
            if (e->op==H_WRITE && e->a[1]==0x6013d5 && index==0x1f) cr1f=e->a[3];
            if (e->op==H_WRITE && e->a[1]==0x6013d5 &&
                (index==0x1a || index==0x21 || index==0x44)) { assert(cr1f==0x57); ++extended; }
            if (e->op==H_WRITE && (e->a[1]==0x600800 || e->a[1]==0x602800) &&
                (e->a[3]==0x1110 || e->a[3]==0x2220)) ++origins;
            assert(!(e->op==H_WRITE && e->a[0]==GF_MODE_VRAM_BAR));
            assert(!(e->op==H_WRITE && e->a[1]==0x6808b0));
        }
        assert(origins==2 && cr1f==0x57 && extended);
        gf4_hw_state_reset();
        assert(cint_prod.chan.state==GF2_OFF && cint_prod.display.engaged);
        assert(cint_prod.display.display4==&display);
        assert(!gf2_host_memcmp(&scan,&cint_service->scan_node.value,sizeof(scan)));
        /* command.c:143-156 issues one reference_test in a healthy F2
         * stop. No additional F3 fini fence may precede it (X4). */
        const u32 f2_stop_references=1;
        assert(cint_references-references==f2_stop_references);
        cint_closed();
        /* Remove this test's synthetic loan only after inspecting retention;
         * the real F4 owner performs final reservation cleanup in C2-service. */
        cint_prod.display.display4=0;
        cint_prod.display.io.timing.clock_us=cint_host.f.io.timing.clock_us;
        cint_trace_clear();
        assert(!gf_display_stop(&cint_prod.display));
        cint_no_resources();
    }
}
/* A running F3-only scan is a valid lower owner to stop. Keep F4 heads
 * disabled here so this test isolates the video_stop dispatch from modeset.
 * The F3-owned nodes are released explicitly after checking F4's result. */
static struct gf4_ctx stop_ctx;
static struct gf4_state stop_state;
static struct gf4_host stop_host;
/* One close/cleanup pass per stop, nouveau_drm.c:955-988. Retry adds a
 * second entry; this count is independent of service implementation. */
enum { STOP_PASSES_PER_ENTRY=1 };
static u32 stop_deadline, stop_admissions;
static i32 stop_admission(void *owner,bool open)
{
    assert(owner==&stop_ctx && !open);
    assert(stop_ctx.deadline==stop_deadline);
    ++stop_admissions;
    return gf3_service_admission(cint_service,false);
}
static struct gf4_links stop_links;
static bool scan_stop_fail;
static int scan_stop_record(struct gf_display_context *c)
{
    if (scan_stop_fail) {
        gf3_host_fail_at(&cint_host,H_WRITE,cint_host.calls[H_WRITE]+1U,CINT_IO_FAILURE);
        scan_stop_fail=false;
    }
    int rc=cint_foundation_stop(c);
    if (rc) c->target=RECOVER_PBUS1854;
    return rc;
}
static void normal_clock_dispatch(void)
{
    const i32 results[]={0,CINT_RESTORE_FAILURE};
    for (u32 i=0;i<sizeof(results)/sizeof(results[0]);++i) {
        cint_reset(); cint_start();
        gf2_host_memset(&stop_ctx,0,sizeof(stop_ctx));
        gf2_host_memset(&stop_state,0,sizeof(stop_state));
        gf4_host_init(&stop_host,&stop_state);
        gf3_host_current=&cint_host;
        stop_ctx.ch=&cint_prod.chan; stop_ctx.accel=&cint_prod.accel;
        stop_ctx.state=&stop_state;
        stop_links=stop_host.links;
        stop_links.owner=&stop_ctx; stop_links.admission=stop_admission;
        assert(!gf4_service_bind(&stop_ctx,&cint_prod.display,&stop_links));
        service_ctx=0; service_slot=0;
        assert(!gf5_service_init(&cint_prod,&stop_ctx,&guard_f5_slot));
        assert(!gf3_service_owner_bind(cint_service,gf4_service_owner_table()));
        stop_ctx.ready=true; stop_ctx.accepting=true;
        stop_ctx.epoch=cint_prod.chan.epoch;
        struct gf_vram_reservation scan=cint_service->scan_node.value;
        cint_prod.display.baseline_valid=true;
        cint_prod.display.baseline_start[0]=0x12000U;
        cint_prod.display.baseline_start[1]=0x34000U;
        cint_trace_clear();
        stop_admissions=0; stop_deadline=cint_deadline();
        cint_restore_rc=results[i];
        /* The real service records dl and calls the real F3/F2/cleanup.
         * A negative cleanup rc must pass through the video entry verbatim. */
        assert(gf_display_stop(&cint_prod.display)==results[i]);
        assert(stop_ctx.deadline==stop_deadline && stop_admissions==STOP_PASSES_PER_ENTRY);
        assert(stop_ctx.last_rc==results[i]);
        assert(cint_prod.chan.state==GF2_OFF && !stop_ctx.ready && !stop_ctx.accepting);
        assert(cint_owner_stops==STOP_PASSES_PER_ENTRY &&
               cint_restore_calls==STOP_PASSES_PER_ENTRY);
        assert(cint_prod.display.display4==&stop_ctx && stop_ctx.legacy==&cint_prod.display);
        assert(!gf2_host_memcmp(&scan,&cint_service->scan_node.value,sizeof(scan)));
        cint_closed();
        if (results[i]) {
            assert(cint_prod.display.engaged && !cint_prod.display.stop_ok);
            assert(cint_prod.display.last_rc==results[i]);
            /* Retry with a fresh entry deadline, retaining the same loans. */
            cint_restore_rc=0; cint_restore_seen=false;
            stop_deadline=cint_deadline();
            assert(!gf_display_stop(&cint_prod.display));
            assert(stop_ctx.deadline==stop_deadline && stop_admissions==STOP_PASSES_PER_ENTRY+STOP_PASSES_PER_ENTRY);
        }
        assert(!cint_prod.display.engaged && cint_prod.display.stop_ok);
        if (!results[i]) {
            /* Completed OFF stops close software loans without hardware writes. */
            cint_trace_clear();
            stop_deadline=cint_deadline();
            assert(!gf_display_stop(&cint_prod.display));
            assert(!cint_host.calls[H_WRITE] && cint_prod.display.stop_ok);
            /* An invalidated completion proof still requires the real lower
             * retry. Preserve its target even with engaged already false. */
            cint_prod.display.stop_ok=false;
            struct gf_display_owner owner=cint_display_owner;
            owner.foundation_stop=scan_stop_record;
            cint_prod.display.owner=&owner;
            cint_restore_seen=false;
            cint_trace_clear();
            scan_stop_fail=true;
            stop_deadline=cint_deadline();
            assert(gf_display_stop(&cint_prod.display)==GF1_EIO);
            assert(cint_prod.foundation.last_rc==GF1_EIO);
            assert(!cint_prod.foundation.revoked && !cint_prod.display.engaged);
            assert(!cint_prod.display.stop_ok && !cint_prod.display.reusable);
            assert(cint_prod.display.last_rc==GF1_EIO);
            assert(cint_prod.display.target==RECOVER_PBUS1854);
            assert(cint_restore_calls==1 && cint_restore_rc==0);
            assert(!gf2_host_memcmp(&scan,&cint_service->scan_node.value,sizeof(scan)));
            /* Both successful START writes would replace the lower target
             * with head B without the first_rc guard in quiesce.c. */
            u32 values[2]={0};
            for (u32 e=0;e<cint_host.nevents;e++) {
                const struct gf3_host_event *event=&cint_events[e];
                if (event->op!=H_WRITE || event->rc) continue;
                if (event->a[1]==0x600800U) values[0]=event->a[3];
                if (event->a[1]==0x602800U) values[1]=event->a[3];
            }
            assert(values[0]==0x12000U && values[1]==0x34000U);
            gf3_host_fail_at(&cint_host,H_WRITE,0,0);
            cint_restore_seen=false;
            stop_deadline=cint_deadline();
            assert(!gf_display_stop(&cint_prod.display));
            assert(!cint_prod.foundation.last_rc && !cint_prod.display.engaged);
            cint_prod.display.owner=&cint_display_owner;
        }
        /* Only the lower F3 fixture owns these nodes; F4 cannot free them. */
        assert(!gf3_service_release(cint_service));
        assert(!gf3_service_owner_bind(cint_service,0));
        cint_no_resources();
    }
}
static void scan_origins(void)
{
    for (u32 fail=0;fail<4U;fail++) {
        cint_reset(); cint_start();
        assert(!gf3_service_stop(cint_service));
        cint_prod.display.baseline_valid=true;
        cint_prod.display.baseline_start[0]=0x12000U;
        cint_prod.display.baseline_start[1]=0x34000U;
        gf3_host_set(&cint_host,0,0x600800U,0x9e000U);
        gf3_host_set(&cint_host,0,0x602800U,0x7e000U);
        cint_trace_clear();
        /* Ordinary stop: first lower write fails. Revoked stop skips those
         * writes, so inject separately into the first scan restore (case 3).
         * Case 2 allows scan writes, but cint_restore rejects revocation;
         * the non-engaged retry above covers successful local restoration. */
        if (fail==1U || fail==3U) gf3_host_fail_at(&cint_host,H_WRITE,1,CINT_IO_FAILURE);
        if (fail>=2U) cint_prod.foundation.revoked=true;
        struct gf_display_owner owner=cint_display_owner;
        owner.foundation_stop=scan_stop_record;
        cint_prod.display.owner=&owner;
        int rc=gf_display_quiesce(&cint_prod.display);
        assert((rc!=0)==(fail!=0));
        if (fail>=2U) assert(rc==GF1_EPCI);
        if (fail) assert(cint_prod.display.target==RECOVER_PBUS1854);
        u32 values[2]={0};
        for (u32 i=0;i<cint_host.nevents;i++) {
            const struct gf3_host_event *e=&cint_events[i];
            if (e->op==H_WRITE && e->a[1]==0x600800U) values[0]=e->a[3];
            if (e->op==H_WRITE && e->a[1]==0x602800U) values[1]=e->a[3];
        }
        assert(values[0]==0x12000U && values[1]==0x34000U);
        if (fail) assert(cint_prod.display.engaged && cint_service->scan_node.value.owner);
        gf3_host_fail_at(&cint_host,H_WRITE,0,0);
        cint_prod.foundation.revoked=false;
        cint_prod.display.owner=&cint_display_owner;
        cint_restore_seen=false;
        assert(!gf_display_stop(&cint_prod.display));
        cint_no_resources();
    }
}
void _start(void)
{
    assert(gf_display_quiesce(0)==OS32_ERR_INVAL);
    production_dispatch();
    quiesce_retains_reservations();
    quiesce_first_error();
    quiesce_row_failures();
    failed_clock_cleanup();
    normal_clock_dispatch();
    scan_origins();
    gf2_host_exit(0);
}
