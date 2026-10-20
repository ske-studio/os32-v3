/* Product guard and exec/ISR slices. IRQ/CR3 and nonreturning kernel
 * transitions are modeled; no signal/alternate stack is claimed to model
 * delivery through the IDT. Guest injection remains separate.
 * TASK_FONTPERF F6: the guard has no x87 save/restore and no NOSYS any more;
 * test_font_guard.py checks the guard object has no x87 instruction. */
#include "font_guard.h"
#include "appslot.h"
#include "exec.h"
#include "io.h"
#include "pgalloc.h"
#include "exec_heap.h"
#include "v86_call.h"
static void die(int rc) __attribute__((noreturn));
static void die(int rc) { __asm__ volatile("int $0x80"::"a"(1),"b"(rc)); for (;;) {} }
static void say(const char *s) { u32 n=0; while(s[n]) n++; __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory"); }
#define CHECK(c, msg) do { if (!(c)) { say("FAIL " msg "\n"); die(1); } } while (0)
volatile u32 kctx_irq_depth, kctx_exc_depth;
static AppSlot app, *g_cur_app = &app;
static u32 frame[APP_FRAME_WORDS], *g_cur_frame=frame;
static int stop_pending, g_longjmp_reason, g_longjmp_id, parks, aborts, calls;
static int ring3_in_syscall=1;
volatile int ring3_wm_depth=1;
static u32 ring3_abort_count;
volatile u32 ring3_park_reject_count;
#define EXEC_LJ_PARK 2
static void *jump[5];
static u32 fault_vector, fault_address, fault_pc;
static int fault_seen;
int appslot_cur(void) { return 2; }
AppSlot *appslot_get(int id) { (void)id; return &app; }
int appslot_stop_pending(void) { return stop_pending; }
int appslot_park_check(void) { return 0; }
int appslot_park_kbd_check(void) { return 0; }
int appslot_park_poll_check(u32 tick) { (void)tick; return 0; }
int appslot_park_yield_check(void) { return 0; }
void appslot_park_commit(void) { parks++; }
void appslot_park_kbd_commit(void) { parks++; }
void appslot_park_poll_commit(void) { parks++; }
void appslot_park_yield_commit(void) { parks++; }
void appslot_park_stop_commit(void) { parks++; }
int v86_call_pinned(void) { return 0; }
int con_sink_is_enabled(void) { return 1; }
void exec_heap_save_state(u32 *n) { *n=0; }
static void ring3_context_clear(void) {}
static void exec_restore_context(int id) { (void)id; }
u32 paging_kernel_pd_phys(void) { return 0x1000; }
void paging_load_cr3(u32 n) { (void)n; }
void exec_longjmp(u32 *buf) { (void)buf; __builtin_longjmp(jump,1); }
void ring3_abort_kill(void) {
    CHECK(!font_guard_active, "STOP after leave");
    aborts++;
}
void font_guard_fault(u32 vector,u32 pc,u32 addr) {
    fault_seen++; fault_vector=vector; fault_pc=pc; fault_address=addr;
    __builtin_longjmp(jump,1);
}
void ring3_fault_kill(void) { fault_seen=-1; __builtin_longjmp(jump,1); }
static u32 sqlite_engine_active;
static void sqlite_engine_fault(u32 v,u32 e,u32 a) { (void)v;(void)e;(void)a; CHECK(0,"sqlite attribution"); }
static void sputs(const char *s) { (void)s; }
static void sput_hex32(u32 v) { (void)v; }
#include "exec.inc"
#include "fault.inc"
static int unexpected_callback(void *context) { (void)context; calls++; return 123; }
static int callback(void *context) {
    int failure=(int)(uptr)context;
    calls++;
    CHECK(font_guard_active,"active inside");
    CHECK(font_guard_run(unexpected_callback,0)==OS32_ERR_BUSY,"reentry");
    app.abort_req=1; stop_pending=1;
    ring3_abort_check();
    CHECK(app.abort_req && !aborts,"STOP retained");
    CHECK(exec_park()==OS32_ERR_BUSY,"wait park held");
    CHECK(!exec_park_kbd(),"kbd park held");
    CHECK(!exec_park_poll(7),"poll park held");
    CHECK(exec_sys_yield()==OS32_ERR_BUSY,"yield park held");
    ring3_wm_depth=0;
    exec_park_stop(frame);
    ring3_wm_depth=1;
    CHECK(!parks,"STOP park held");
    return failure ? -77 : 42;
}
static void normal(void) {
    app.gui=app.cpl3=1; app.state=APP_STATE_RUNNING;
    for (int fail=0;fail<2;fail++) {
        int old=calls;
        aborts=parks=0;
        CHECK(font_guard_run(callback,(void *)(uptr)fail)==(fail ? -77:42) && calls==old+1,"callback result");
        CHECK(!font_guard_active,"active cleared");
        ring3_abort_check();
        CHECK(aborts==1 && !app.abort_req,"STOP delivered once");
    }
    kctx_irq_depth=1; CHECK(font_guard_run(callback,0)==OS32_ERR_BUSY,"IRQ refusal");
    kctx_irq_depth=0; kctx_exc_depth=1;
    CHECK(font_guard_run(callback,0)==OS32_ERR_BUSY,"exception refusal");
    kctx_exc_depth=0;
    CHECK(font_guard_run(0,0)==OS32_ERR_INVAL,"null callback");
    /* After leave each real park path may commit at its existing safe point. */
    ring3_wm_depth=0; stop_pending=1;
    for (volatile int i=0;i<5;i++) {
        int prior=parks;
        if (!__builtin_setjmp(jump)) {
            if(i==0) exec_park();
            if(i==1) exec_park_kbd();
            if(i==2) exec_park_poll(7);
            if(i==3) exec_sys_yield();
            if(i==4) exec_park_stop(frame);
            CHECK(0,"park must transfer");
        }
        CHECK(parks==prior+1,"park after leave");
    }
    say("GREEN active/reentry/STOP/park/refusals\n");
}
static int fault_body(void *context) {
    u32 regs[13]={0}; /* CPL0 syscall/WM context, as actual font callbacks. */
    if ((uptr)context==14) page_fault_handler(0,0xdead1000,0x123456,regs);
    else exception_handler(0,(u32)(uptr)context,0x123456,regs);
    CHECK(0,"fault must not return"); return 0;
}
static void faults(void) {
    static const u32 vectors[]={6,7,13,14,16};
    for (u32 i=0;i<5;i++) {
        fault_seen=0; font_guard_active=0; ring3_in_syscall=1;
        if (!__builtin_setjmp(jump)) font_guard_run(fault_body,(void *)(uptr)vectors[i]);
        CHECK(fault_seen==1 && font_guard_active,"font before app kill");
        CHECK(fault_vector==vectors[i] && fault_pc==0x123456,"fault identity");
        CHECK(fault_address==(vectors[i]==14 ? 0xdead1000u:0),"fault address");
        /* Simulated reboot after terminal fail-stop; do not claim recovery. */
        font_guard_active=0;
        fault_seen=0;
        if (!__builtin_setjmp(jump)) fault_body((void *)(uptr)vectors[i]);
        CHECK(fault_seen==-1,"ordinary app fault unchanged");
    }
    say("GREEN font fault attribution / ordinary app fault\n");
}
void _start(void) { normal(); faults(); die(0); }
