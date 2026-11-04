/* C2-service-stop, GF4 UNIT14: real F1/F2/F3/V0 and legacy display stop.
 * Reuse C1's integration hardware oracle, not fake service dependencies.
 * Link B4 and F4 service dependencies separately, as for gf_accel_c1_host.c.
 * PM must first integrate C2-service-f3. No libc; entry _start.
 * Ordering oracle: MIT nouveau c03c1a1 nouveau_drm.c:944-988 (display,
 * idle/fence, objects). Local quiesce values are the PC-98 coexistence
 * contract, hwdev/gf/quiesce_rows.h, not a nouveau register sequence. */
#include "../../include/io.h"
#define irq_save() 0U
#define irq_restore(flags) ((void)(flags))
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#define GF3_CINT_C2
#include "gf_accel_c1_host.c"
#ifndef GF4_COMMAND_TEST_SOURCE
#define GF4_COMMAND_TEST_SOURCE "../../hwdev/gf/command_test.c"
#endif
#include GF4_COMMAND_TEST_SOURCE

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
    cint_reset();
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
    cint_prod.display.engaged=false;
    assert(!gf3_service_owner_bind(cint_service,0));
    assert(!gf_display_stop(&cint_prod.display));
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
        cint_prod.display.display4=&display;
        struct gf_vram_reservation scan=cint_service->scan_node.value;
        cint_trace_clear();
        u32 references=cint_references;
        if (missing) cint_prod.display.io.timing.clock_us=0;
        else gf3_host_fail_at(&cint_host,H_CLOCK,1,CINT_IO_FAILURE);
        assert(gf_display_stop(&cint_prod.display)==-1);
        assert(cint_prod.display.last_rc==GF2_EIO && display.last_rc==GF2_EIO);
        assert(!display.accepting && !display.reusable);
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
void _start(void)
{
    assert(gf_display_quiesce(0)==OS32_ERR_INVAL);
    production_dispatch();
    quiesce_retains_reservations();
    quiesce_first_error();
    quiesce_row_failures();
    failed_clock_cleanup();
    gf2_host_exit(0);
}
