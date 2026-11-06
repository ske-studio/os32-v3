/* U6: production service -> resident poll -> backend stop -> recovery.
 * Build with the scanlog source set and the extracted foreground entries
 * described in run/gt-u6/run-foreground.py. U7 owns CHECK/runner registration.
 * Only CPU halt, route ports, allocator/time and GUI syscall boundary are
 * host seams. No thermal/service/stop callback is replaced with a fake.
 * ROM/PLL/GPIO fixtures are inherited from gf4_scanlog_host.c (MIT nouveau
 * c03c1a1); critical pause ownership and cooperative scheduling are OS32 policy.
 */
#if defined(U6_GPIO)
#include "gf5_gpio_diag_host.c"
#elif defined(U6_OBSERVE)
#include "gf5_observe_host.c"
#else
#include "../../lib/serial_diag.h"
void u6_unused_serial(const char *);
int u6_unused_serial_gate(void);
#define geforce_display_failed u6_unused_display_failed
#define serial_puts_polled u6_unused_serial
#define serial_gate_active u6_unused_serial_gate
#define GF4_SCANLOG_ENTRY u6_unused_scanlog_start
#include "gf4_scanlog_host.c"
#undef GF4_SCANLOG_ENTRY
#undef serial_puts_polled
#undef serial_gate_active
#undef geforce_display_failed

void u6_resident_request(i32,i32,i32);
#undef irq_save
#undef irq_restore
static void u6_disable(void);
static void u6_halt(void);
/* The backend has its module-local forwarder; the resident side is a
 * different translation unit in the OS. Rename only that public definition. */
#define geforce_heat_stop_request u6_resident_request
#define _disable u6_disable
#define _halt u6_halt
#include "../../drivers/video/geforce/geforce_module.c"
#undef _halt
#undef _disable
#undef geforce_heat_stop_request

#include "../../hwdev/gf/gf2_test.h"
#include "../../exec/appslot.h"
#include "../../exec/exec.h"
#include "../../kernel/gui.h"
#include "serial_diag_host.h"

static bool u6_safe=true, u6_emergency_halt;
static u32 u6_notifications,u6_aborts,u6_syncs,u6_halts,u6_resets;
static bool u6_gui, u6_caller, u6_wm;
static int ring3_in_syscall,font_guard_active;
static AppSlot u6_app, u6_owner_app, *g_cur_app;
static u32 *g_cur_frame;
volatile u32 ring3_park_reject_count;
static GeforceModuleContext u6_module;
volatile u32 tick_count;
static bool kbd_gui_mode;
#include "../../drivers/input/kbd_fgpoll.inc"

int pc98_route_safe(void) { return u6_safe; }
int gf_production_return(void) { return geforce_return(&c1_platform); }
int vfs_sync(void)
{
    assert(!active && !routed && foreground_poll_busy);
    ++u6_syncs;
    return -88;
}
static void u6_disable(void)
{
    assert(u6_syncs==1 && host_serial_find("sync_rc=-88"));
}
static void u6_halt(void)
{
    ++u6_halts;
    if (u6_emergency_halt) {
        assert(!active && !routed && !u6_notifications && !u6_aborts);
        assert(u6_syncs==1 && heat_bridge.stop_state.state==GF5_STOP_REQUESTED);
        assert(host_serial_find("reason=") && host_serial_find("milli_c=135000"));
        gf2_host_exit(0);
    }
    /* Normal sys_halt may return in this host seam; an unexpected thermal
     * halt must fail at runtime rather than spin in the product halt loop. */
    assert(!foreground_poll_busy);
}
void platform_reset(void)
{
    assert(!u6_safe && ++u6_resets==1);
    assert(scanlog_count("GF4 STATE stage=leave98 stop_rc=")==1);
    gf2_host_exit(0);
}
int con_sink_is_enabled(void) { return u6_gui; }
static int u6_display_owner=APP_ID_MIN;
int appslot_cur(void) { return APP_ID_MIN; }
int appslot_gfx_owner(void) { return u6_display_owner; }
AppSlot *appslot_get(int id) { if (id==APP_ID_SHELL) return &u6_owner_app; assert(id==APP_ID_MIN); return &u6_app; }
int appslot_park_poll_check(u32 tick) { (void)tick; return -1; }
int appslot_park_yield_check(void) { return -1; }
void appslot_park_poll_commit(void) { assert(false); }
void appslot_park_yield_commit(void) { assert(false); }
static void exec_park_transfer(int id,AppSlot *a,const u32 *f,void (*commit)(void))
{ (void)id;(void)a;(void)f;(void)commit; assert(false); }
static void caller_access_leave(const int *previous) { u6_caller=!!*previous; }
static void exec_cui_display_abort(void) {}
static void ring3_abort_check(void)
{
    assert(!foreground_poll_busy && !cint_prod.feature5.busy);
    if (u6_app.abort_req) ++u6_aborts;
}
static void exec_park_stop(u32 *f) { (void)f; }
static void gfx_reinit_surfaces(int start) { assert(!start); }
static int gfx_started;
static GuiHandler g_gui_handler;
static int g_cur=APP_ID_MIN;
void ring3_wm_enter(void) { assert(!u6_caller && !u6_wm); u6_wm=true; }
void ring3_wm_leave(void) { assert(u6_wm); u6_wm=false; }
/* Actual sys_halt/reboot, yield/poll, syscall completion, display_failed,
 * gui_display_failed and appslot_abort_request bodies, extracted verbatim.
 * CPU halt alone is substituted by the driver. */
#include "gf5_foreground_entries.h"

static i32 u6_session_request(u32 op,u32 action,int owner)
{
    assert(u6_wm && !u6_caller && !active && !routed && !scanlog_route);
    assert(op==GUI_OP_SESSION_REQUEST && action==GUI_SESSION_SWITCH_CUI);
    assert(owner==GUI_SHELL_OWNER);
    ++u6_notifications;
    return 0;
}
static void u6_setup(void)
{
    foreground_poll_busy=foreground_deadline_valid=false;
    heat_bridge=(struct geforce_heat_bridge){.request=u6_resident_request};
    u6_notifications=u6_aborts=u6_syncs=u6_halts=0;
    u6_safe=true; u6_emergency_halt=false; u6_caller=u6_wm=false;
    u6_app=(AppSlot){.cpl3=1,.state=APP_STATE_RUNNING};
    u6_owner_app=(AppSlot){.cpl3=1,.state=APP_STATE_RUNNING};
    g_cur_app=&u6_app; g_cur_frame=0; font_guard_active=0;
    u6_gui=false; kbd_gui_mode=false; g_gui_handler=u6_session_request;
    u6_module=c1_context; u6_module.heat=&heat_bridge;
    u6_module.feature5_out=&feature5_slot;
    geforce_context=&u6_module;
    c1_platform.opaque=&cint_prod;
    scanlog_init();
    registered=1; feature5_slot=scanlog_f5_ops;
    assert(heat_bridge.stop && heat_bridge.clear);
    scanlog_heat_fixture();
    startlog_clear();
    geforce_foreground_poll();
    assert(cint_prod.feature5.fan.sensor_setup);
}
static void u6_sample(u32 degrees)
{
    struct gf5 *g=&cint_prod.feature5;
    gf3_host_set(&cint_host,0,0x15b4,degrees);
    cint_host.now=(u32)g->next_sensor;
    tick_count+=PIT_HZ;
    startlog_clear();
}
static i32 (*u6_drain)(void *);
static u32 u6_drains;
static i32 u6_count_drain(void *owner)
{
    ++u6_drains;
    return u6_drain(owner);
}
static void u6_critical(void)
{
    u6_setup();
    struct gf5 *g=&cint_prod.feature5;
    u6_drains=0; u6_drain=g->links.drain_pause;
    g->links.drain_pause=u6_count_drain;
    u6_sample(105); geforce_foreground_poll();
    assert(u6_drains==1 && g->heat.critical_paused && !g->accepting);
    assert(!g->stop->state);
    assert(active && routed && scanlog_route && g->display->ready);
    struct gf5_clock clock;
    assert(scanlog_f5_ops->clock_read(g,&clock)==GF5_ESTATE);
    u6_sample(101); geforce_foreground_poll();
    assert(g->heat.critical_paused && u6_drains==1);
    u6_sample(99); geforce_foreground_poll();
    assert(!g->heat.critical_paused && g->accepting && active && routed);
    u6_sample(105); geforce_foreground_poll();
    assert(u6_drains==2 && g->heat.critical_paused);
    g->display->saved_closed=true;
    u6_sample(99); geforce_foreground_poll();
    assert(g->heat.critical_paused && !g->accepting);
    g->display->saved_closed=false;
    u6_sample(99); geforce_foreground_poll();
    assert(!g->heat.critical_paused && g->accepting);
    scanlog_finish();
}
/* Separate from the empty-table critical fixture: one reachable lower
 * pstate, v0x21 core MHz at +6, memory at +11 (bios/perf.c). */
static void u6_downclock_stop(bool sensor_failure)
{
    u6_setup();
    struct gf5 *g=&cint_prod.feature5;
    scanlog_put(S_PERF+2,1,1);
    scanlog_put(S_PERF+8+6,108,2);
    scanlog_put(S_PERF+8+11,270,2);
    gf5_rom_release(g);
    assert(!gf5_rom_acquire(g,scanlog_rom,sizeof(scanlog_rom),0));
    u6_sample(96);
    if (!sensor_failure)
        /* ADC write is first; first PLL write must fail at runtime. */
        gf3_host_fail_at(&cint_host,H_WRITE,cint_host.calls[H_WRITE]+2,-881);
    geforce_foreground_poll();
    if (sensor_failure) {
        assert(g->heat.down_active && !g->stop->state && active && routed);
        u6_sample(97);
        gf3_host_fail_at(&cint_host,H_READ,cint_host.calls[H_READ]+1,-882);
        geforce_foreground_poll();
        assert(!g->fan.sensor_poll);
    }
    assert(g->stop->state && g->stop->reason==GF5_EIO);
    assert(heat_bridge.stop_state.source_rc==(sensor_failure ? -882 : -881));
    assert(heat_bridge.stop_state.milli_c==96000 && heat_bridge.stop_state.reason==GF5_EIO);
    assert(!active && !routed && !scanlog_route && !(heat_bridge.stop_state.state==GF5_STOP_REQUESTED));
    assert((heat_bridge.stop_state.state!=GF5_STOP_NORMAL) && u6_app.abort_req && !geforce_probe());
    if (sensor_failure) {
        u6_sample(92);
        assert(!geforce_heat_latch_clear() && !(heat_bridge.stop_state.state!=GF5_STOP_NORMAL));
        assert(g->fan.sensor_poll && !g->next_sensor && g->fan.sensor_setup==0xffU);
        geforce_init();
        assert(active && routed && scanlog_route);
        u6_app.abort_req=0;
        u6_sample(96);
        u32 reads=cint_host.calls[H_READ];
        geforce_foreground_poll();
        assert(cint_host.calls[H_READ]>reads && g->next_sensor>cint_host.now);
        struct gf5_clock clock;
        assert(!scanlog_f5_ops->clock_read(g,&clock));
        assert(g->heat.down_active && clock.core_khz==108000 && !(heat_bridge.stop_state.state!=GF5_STOP_NORMAL));
        assert(!u6_app.abort_req && active && routed);
    }
    scanlog_finish();
}
/* Diagnostic-only allocation is outside these production dispatch cases. */
const struct gf2_dma_ops *geforce_dma_ops(void) { assert(false); return 0; }
/* Serial stop/start uses the production owner. Model the absence of a gfx
 * client by clearing only HAL bookkeeping, leaving all device state real. */
static void u6_serial_start(void)
{
    u6_setup();
    gf2_test_bind_production(&cint_prod.foundation,&cint_prod.chan);
    struct gf2_test_request q={sizeof(q),GF2T_STOP,cint_host.now+GF2_TIMEOUT_US};
    struct gf2_test_reply reply;
    assert(!gf2_module_ops.call(&cint_prod.foundation,&q,&reply));
    active=routed=0;
    q.op=GF2T_START;
    q.deadline_us=cint_host.now+GF2_TIMEOUT_US;
    assert(!gf2_module_ops.call(&cint_prod.foundation,&q,&reply));
    assert(!active && !routed && cint_prod.chan.state==GF2_RUN);
    assert(cint_prod.display.engaged && cint_prod.feature5.display->ready);
    startlog_clear();
}
static void u6_serial_shutdown(void)
{
    u6_serial_start();
    u6_sample(135); geforce_foreground_poll();
    assert(cint_prod.chan.state==GF2_OFF && !cint_prod.display.engaged);
    assert(!active && !routed && !scanlog_route && (heat_bridge.stop_state.state!=GF5_STOP_NORMAL) && !(heat_bridge.stop_state.state==GF5_STOP_REQUESTED));
    assert(heat_bridge.stop_state.milli_c==135000 && heat_bridge.stop_state.reason==GF5_ESTATE);
    assert(scanlog_count("GF4 STATE stage=leave98 stop_rc=")==1);
    assert(!u6_app.abort_req && !u6_notifications && !u6_aborts);
    scanlog_finish();
}
static void u6_serial_critical(void)
{
    u6_serial_start();
    struct gf5 *g=&cint_prod.feature5;
    u6_drains=0; u6_drain=g->links.drain_pause;
    g->links.drain_pause=u6_count_drain;
    u6_sample(105); geforce_foreground_poll();
    assert(u6_drains==1 && g->heat.critical_paused && !g->accepting);
    assert(!active && !routed && g->display->ready && cint_prod.display.engaged);
    assert(!(heat_bridge.stop_state.state!=GF5_STOP_NORMAL) && !u6_app.abort_req);
    u6_sample(99); geforce_foreground_poll();
    assert(!g->heat.critical_paused && g->accepting);
    scanlog_finish();
}
static void u6_hot_leave(void)
{
    u6_setup();
    struct gf5 *g=&cint_prod.feature5;
    u6_sample(96);
    assert(!geforce_stop());
    assert(!active && !routed && g->fan.sensor_poll);
    /* A different app is now running on 98. Thermal polling still runs. */
    startlog_clear();
    u6_sample(135); geforce_foreground_poll();
    assert(g->stop->state==GF5_STOPPED);
    assert(heat_bridge.stop_state.reason==GF5_ESTATE && heat_bridge.stop_state.milli_c==135000);
    assert(scanlog_count("GF4 STATE stage=leave98")==1 && !scanlog_route);
    assert(!u6_app.abort_req && !u6_notifications && !u6_aborts && !u6_syncs && !u6_halts);
    assert(!geforce_probe());
    u6_safe=true;
    scanlog_finish();
}
static void u6_cooperative(void)
{
    u6_setup();
    struct gf5 *g=&cint_prod.feature5;
    for (u32 entry=0;entry<4;++entry) {
        u6_sample(30);
        u64 old=g->next_sensor;
        if (entry==0) kbd_foreground_poll();
        if (entry==1) sys_halt();
        if (entry==2) assert(!exec_sys_yield());
        if (entry==3) { u32 frame[APP_FRAME_WORDS]={0}; g_cur_frame=frame;
                       assert(!exec_park_poll(tick_count)); }
        assert(g->next_sensor>old && !g->busy && !foreground_poll_busy);
        u64 next=g->next_sensor;
        geforce_foreground_poll();
        assert(g->next_sensor==next);
    }
    /* Delayed work does one measurement, never a catch-up burst. */
    cint_host.now+=5000000; startlog_clear();
    geforce_foreground_poll();
    assert(g->next_sensor==(u64)cint_host.now+GF5_SENSOR_INTERVAL_US);
    /* Reentry cannot acquire the module loan or touch MMIO, even when due. */
    cint_host.now=(u32)g->next_sensor;
    u32 reads=cint_host.calls[H_READ];
    foreground_poll_busy=true; geforce_foreground_poll();
    assert(cint_host.calls[H_READ]==reads); foreground_poll_busy=false;
    /* IRQ0 may dispatch foundation work, but must not execute therm MMIO. */
    u64 deadline=g->next_sensor;
    cint_host.now=(u32)deadline;
    geforce_tick();
    assert(g->next_sensor==deadline);
    scanlog_finish();
}
static void u6_fault_monitor(void)
{
    for (u32 saved=0;saved<2;++saved) {
        u6_setup();
        struct gf5 *g=&cint_prod.feature5;
        g->phase=saved ? GF5_SAVED : GF5_FAULT;
        g->accepting=false; g->ch->fault.latched=!saved;
        struct gf5_clock clock;
        u32 reads=cint_host.calls[H_READ];
        assert(scanlog_f5_ops->clock_read(g,&clock)==GF5_ESTATE);
        assert(cint_host.calls[H_READ]==reads);
        u6_sample(30);
        reads=cint_host.calls[H_READ];
        geforce_foreground_poll();
        assert(cint_host.calls[H_READ]>reads && g->next_sensor>cint_host.now);
        assert(g->next_auto>cint_host.now); /* fan's independent alarm also runs */
        volatile u8 *bar=g->ch->f->bar0;
        g->ch->f->bar0=0;
        u6_sample(135); reads=cint_host.calls[H_READ];
        geforce_foreground_poll();
        assert(cint_host.calls[H_READ]==reads && !(heat_bridge.stop_state.state!=GF5_STOP_NORMAL));
        g->ch->f->bar0=bar;
        u6_sample(135);
        geforce_foreground_poll();
        assert((heat_bridge.stop_state.state!=GF5_STOP_NORMAL) && !active && !routed && !(heat_bridge.stop_state.state==GF5_STOP_REQUESTED));
        scanlog_finish();
    }
}
static void u6_shutdown(void)
{
    for (u32 gui=0;gui<2;++gui) {
        u6_setup(); u6_gui=!!gui; u6_app.gui=!!gui;
        /* The GUI app is not the shell/display owner. No gfx_shutdown owner
         * privilege is supplied. Execute after caller_access_leave, P3. */
        u6_sample(135);
        u32 frame[APP_FRAME_WORDS]={0};
        u6_caller=true; ring3_in_syscall=1;
        u6_syscall_tail(frame);
        assert(!u6_caller && !u6_wm && !foreground_poll_busy);
        assert(!active && !routed && !scanlog_route && (heat_bridge.stop_state.state!=GF5_STOP_NORMAL));
        assert(!u6_notifications && u6_aborts==1 && u6_app.abort_req);
        assert(!geforce_probe()); /* Another app's ordinary gfx start falls back. */
        geforce_init(); assert(!active && !routed);
        u6_sample(120);
        int rc=geforce_heat_latch_clear();
        assert(rc==GF5_EBUSY && (heat_bridge.stop_state.state!=GF5_STOP_NORMAL));
        u6_sample(92);
        assert(!geforce_heat_latch_clear() && !(heat_bridge.stop_state.state!=GF5_STOP_NORMAL));
        assert(geforce_probe());
        scanlog_finish();
    }
}
void _start(void)
{
    scanlog_map(CINT_FB_PHYS,GF_MODE_VRAM_BYTES);
    scanlog_map(SCANLOG_BAR0,0x101000U);
    u6_setup();
    struct gf5 *retry=&cint_prod.feature5;
    u64 previous=retry->next_sensor;
    scanlog_clock_error=true; geforce_foreground_poll();
    assert(retry->next_sensor==previous && !retry->poll_off);
    scanlog_clock_error=false; u6_sample(60); geforce_foreground_poll();
    assert(retry->next_sensor>previous && !gf5_stopping(retry));
    scanlog_finish();
    u6_setup(); u6_gui=true; u6_app.gui=true; u6_display_owner=APP_ID_SHELL;
    u6_sample(135); geforce_foreground_poll();
    assert(heat_bridge.stop_state.state==GF5_STOPPED && !u6_app.abort_req && u6_owner_app.abort_req);
    assert(!u6_notifications); /* A different running GUI app is never reclaimed. */
    u6_resident_request(-999,1,-999);
    assert(heat_bridge.stop_state.reason==GF5_ESTATE && heat_bridge.stop_state.milli_c==135000);
    scanlog_finish(); u6_display_owner=APP_ID_MIN;
    u6_critical();
    u6_downclock_stop(false); u6_downclock_stop(true);
    u6_serial_shutdown(); u6_serial_critical();
    u6_hot_leave();
    u6_cooperative(); u6_fault_monitor(); u6_shutdown();
    u6_setup();
#ifdef U6_STOPFAIL
    svc_close_rc=SVC_INJECT;
    /* Both the ordinary close and the lower engine reset fail. Route is safe. */
    u6_resident_request(GF5_ESTATE,135000,0);
    gf3_host_fail_at(&cint_host,H_WRITE,0,-883);
#else
    u6_safe=false;
#endif
#ifdef U6_REBOOT
    /* R2-2: thermal no-route halt must not infect sys_reboot. */
    sys_reboot();
#else
    u6_emergency_halt=true; host_serial_reset();
    u6_sample(135); geforce_foreground_poll();
#endif
    assert(false);
    gf2_host_exit(1);
}
#endif

/* U6_RUST_BEGIN
// The U7 driver appends this test to a private copy of host/wm_tests.rs.
// The C half asserts the exact op/arg/owner emitted after hardware stop;
// this half feeds that tuple into the real gshell handler and recovery.
#[test]
fn u6_heat_shutdown_recovers_gui_fullscreen() {
    use crate::{fullscreen, handler, mocks, multiapp, session, wm};
    use os32api::gui::proto::GUI_OP_OWNER_EXIT;
    mocks::init();
    session::clear();
    let shm = mocks::Shm::new();
    two_app_global(&shm);
    mocks::set_screen_owner(3);
    assert!(crate::after_exec(wm::g()));
    assert_eq!(fullscreen::owner(), 3);
    // Targeted abort returns through the existing exec owner-exit notification.
    assert_eq!(handler::gshell_gui_handler(GUI_OP_OWNER_EXIT, 0, 3), 0);
    assert_eq!(multiapp::live_count(), 1);
    assert!(mocks::kill_calls().is_empty());
    wm::g().inited = false;
}
U6_RUST_END */
