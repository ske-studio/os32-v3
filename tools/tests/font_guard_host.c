/* Real x87 instructions + product guard and exec/ISR slices. IRQ/CR3 and
 * nonreturning kernel transitions are modeled; no signal/alternate stack is
 * claimed to model delivery through the IDT. Guest injection remains separate. */
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
static u8 before[108], after[108];
static void snapshot(u8 *p) {
    __asm__ volatile("fnsave (%0); fwait; frstor (%0)"::"r"(p):"memory");
}
static int same(const u8 *a, const u8 *b) {
    /* Intel protected-mode 32-bit save: reserved words and high five FOP bits
     * are excluded. All 80 register bytes, including empty tags, are compared. */
    for (u32 i=0; i<108; i++) {
        if (i==2 || i==3 || i==6 || i==7 || i==10 || i==11 || i==26 || i==27) continue;
        u8 mask = i==19 ? 7 : 255;
        if ((a[i]&mask)!=(b[i]&mask)) return 0;
    }
    return 1;
}
void ring3_abort_kill(void) {
    snapshot(after);
    CHECK(!font_guard_active && same(before,after), "STOP after restore");
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
    u16 cw, sw;
    int failure=(int)(uptr)context;
    calls++;
    __asm__ volatile("fnstcw %0; fnstsw %1":"=m"(cw),"=m"(sw));
    CHECK(font_guard_active && cw==0x037f && sw==0,"known CW and clean status");
    snapshot(after);
    CHECK(after[8]==255 && after[9]==255,"empty stack inside");
    CHECK(font_guard_run(unexpected_callback,0)==OS32_ERR_BUSY,"reentry");
    /* Fill all eight slots, change rounding and exception masks inside too. */
    for (int i=0;i<8;i++) __asm__ volatile("fld1":::"st");
    cw=0x0b7e;
    __asm__ volatile("fldcw %0"::"m"(cw));
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
static void seed(u16 cw,int sparse,int pending) {
    __asm__ volatile("fninit");
    /* Sticky invalid status, then eight distinct finite values. */
    __asm__ volatile("fldz; fldz; fdivp; fstp %%st(0)":::"st");
    for (int i=0;i<8;i++) {
        int n=(i+3)*17;
        __asm__ volatile("fildl %0"::"m"(n):"st");
    }
    __asm__ volatile("fincstp; fincstp; fincstp");
    if (sparse) __asm__ volatile("ffree %%st(2); ffree %%st(5)":::"st");
    /* Use FRSTOR to seed a pending unmasked exception without delivering it. */
    __asm__ volatile("fnsave %0; fwait":"=m"(before));
    before[0]=(u8)cw; before[1]=(u8)(cw>>8);
    if (pending) before[4]|=0x81; else before[4]&=~0x81;
    __asm__ volatile("frstor %0"::"m"(before):"memory");
    snapshot(before);
}
u32 host_cr0, host_ac, host_no_fpu;
extern int font_x87_boot_init(void);
static void probe(void) {
    for (u32 cpu=0;cpu<2;cpu++) for(u32 absent=0;absent<2;absent++) {
        host_cr0=0x8000001f; /* PG, ET, TS, EM, MP, PE */
        host_ac=cpu ? 1u<<18 : 0;
        host_no_fpu=absent;
        CHECK(font_x87_boot_init()==!absent,"boot FPU detection");
        CHECK(host_cr0==(0x80000013u | (cpu ? 32u:0)),"boot CR0 386/486");
    }
    say("GREEN probe branches (CR0/absent ESC modeled)\n");
}
static void normal(void) {
    app.gui=app.cpl3=1; app.state=APP_STATE_RUNNING;
    font_x87_present=1;
    for (int r=0;r<4;r++) for(int m=0;m<3;m++) for(int sparse=0;sparse<2;sparse++) for(int fail=0;fail<2;fail++) {
        u16 cw=(u16)(0x0300 | (r<<10) | (m==0 ? 0x7f : m==1 ? 0x7e : 0x40));
        seed(cw,sparse,m==1);
        aborts=parks=0;
        CHECK(font_guard_run(callback,(void *)(uptr)fail)==(fail ? -77:42),"callback result");
        snapshot(after);
        CHECK(!font_guard_active && same(before,after),"full state restored");
        ring3_abort_check();
        CHECK(aborts==1 && !app.abort_req,"STOP delivered once");
        __asm__ volatile("fninit");
    }
    /* No FPU: callback never executes; dispatch tells future caller to use KCG. */
    seed(0x037f,0,0);
    int old=calls;
    font_x87_present=0;
    CHECK(font_guard_run(unexpected_callback,0)==OS32_ERR_NOSYS && calls==old,"no FPU KCG");
    snapshot(after); CHECK(same(before,after),"no FPU no instructions");
    font_x87_present=1;
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
    say("GREEN state/CW/reentry/STOP/park/KCG\n");
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
        font_guard_active=0; __asm__ volatile("fninit");
        fault_seen=0;
        if (!__builtin_setjmp(jump)) fault_body((void *)(uptr)vectors[i]);
        CHECK(fault_seen==-1,"ordinary app fault unchanged");
    }
    say("GREEN font fault attribution / ordinary app fault\n");
}
void _start(void) { probe(); normal(); faults(); die(0); }
