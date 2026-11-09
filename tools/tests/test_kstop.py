"""GUI STOP A: actual AppSlot/dispatch/park/resume code, ILP32 + runtime mutants.
R1's existing harness covers both landings, IRQ/fault cleanup and parked WM kill.
"""
from exec_source import exec_source
from syscall_source import syscall_providers
from kernel_includes import include_flags
import pathlib
import subprocess
import sys
import tempfile
import host32
from test_exec_r1 import function

ROOT = pathlib.Path(__file__).resolve().parents[2]
PRE = r'''
#include "types.h"
#include "host_exec_context.h"
#include <stdbool.h>
/* Thermal dispatch is outside this fixture; keep its syscall gate idle. */
bool geforce_fg_due(u32 now) { (void)now; return false; }
void geforce_foreground_poll(void) {}

static u32 host_cr3;
#include "font_guard_inactive.h"
#include "shell_service_stub_host.h"   /* T7b-b2 S3b: exec.c の結線の弱い贋物 */
#include "appslot.h"
#include "exec.h"
#include "ring3_str.h"
#include "ring3_ls.h"
int ring3_ls_dispatch(const struct kapi_values *values) { (void)values; __builtin_trap(); }
#include "fd_redirect.h"
#include "../drivers/input/kbd.h"
#include "input_service.h"
#include "gui.h"
#include "v86.h"
void v86_int80(u32 *frame) { (void)frame; __builtin_trap(); }
#define CHECK(x) do { if (!(x)) die(__LINE__); } while (0)
static void die(int line) {
    char b[12]; int n=0; unsigned v=line;
    do { b[n++]='0'+v%10; v/=10; } while(v);
    __asm__ volatile("int $0x80"::"a"(4),"b"(2),"c"(b),"d"(n):"memory");
    __asm__ volatile("int $0x80"::"a"(1),"b"(1):"memory"); for (;;) {}
}
static int owner=1, cr3=1, gui=1, invokes, jumped, clears, injected;
static u32 tick_count=100;
static k_jmpbuf saved_buf;
void x87_save(X87State *p) { (void)p; }
static AppSlot *g_cur_app;
static struct arch_syscall *g_cur_frame;
static int ring3_in_syscall, caller;
volatile int ring3_wm_depth;
volatile u32 kctx_irq_depth, kctx_exc_depth;
static int g_longjmp_reason, g_longjmp_id;
#define EXEC_LJ_PARK 1
volatile u32 ring3_caller_reject_count, ring3_abort_count;
static int aborted;
/* This fixture has no SQLite execution; sqlite-engine tests the active case. */
void res_owner_set(int id) { owner=id; }
int res_owner_get(void) { return owner; }
void fd_redirect_save(FdRedirectState *p) { (void)p; }
void fd_redirect_restore(const FdRedirectState *p) { (void)p; }
void fd_redirect_clear_state(FdRedirectState *p) { (void)p; }
void gfx_shutdown(void) { CHECK(0); }
void gfx_policy_reeval(void) {}
static int con_sink_is_enabled(void) { return gui; }
static void ring3_abort_kill(void) { aborted++; }
static void ring3_context_clear(void) { caller=ring3_in_syscall=ring3_wm_depth=0; g_cur_frame=0; clears++; }
static void exec_restore_context(int id) { CHECK(id==1); g_cur_app=0; }
void exec_heap_save_state(u32 *p) { *p=17; }


#define LEGACY_FAKE_TRAP_ENABLE
#define LEGACY_FAKE_TRAP (( ((u32 *)frame)[V86I_EFLAGS] & EFLAGS_VM) ? (v86_int80(frame), 1) : 0)
#define LEGACY_FAKE_LOWMAP_ENABLE
#define LEGACY_FAKE_PINNED_ENABLE
#define LEGACY_FAKE_DISPATCH_ENABLE
#include "legacy_guest_fake.h"

u32 paging_kernel_pd_phys(void) { return 1; }
void paging_load_cr3(u32 p) { cr3=p; }
void exec_longjmp(k_jmpbuf p) {
    CHECK(p==appslot_at(2)->jmpbuf && !caller && !ring3_in_syscall && !ring3_wm_depth);
    CHECK(!kctx_irq_depth && !kctx_exc_depth && owner==1 && cr3==1);
    jumped++; extern void asm_longjmp(k_jmpbuf); asm_longjmp(saved_buf);
}
int caller_access_get_user(struct caller_access *out) { (void)out; return caller; }
int copy_caller_bytes(const struct caller_access *c, const void *s, void *d, u32 n) { (void)c; (void)s; (void)d; CHECK(n==0); return 1; }
int caller_access_enter(CallerAccessFrame *p, enum caller_origin origin) { (void)origin; p->valid=caller; caller=1; return 1; }
void caller_access_leave(const volatile CallerAccessFrame *p) { caller=p->valid; }
#define CALLER_USER 1
static void ring3_gui_pump(void) {}
void ring3_fault_kill(void) { die(__LINE__); }
static u32 table[KAPI_FUNC_COUNT+2];
#define KAPI_ADDR table
#define RING3_USTACK_TOP MEM_APP_STACK_TOP
#define RING3_ARG_WINDOW 64u
const u16 kapi_argsize[KAPI_FUNC_COUNT]={0}, kapi_argptr[KAPI_FUNC_COUNT]={0};
int ring3_ptr_ok(u32 p) { (void)p; return 1; }
static int request_inside;
void ring3_abort_request(void);
#include "os32_kapi_slots.h"
#include "kapi_strings.h"
int kapi_print_call(struct kapi_arg_source *args) { (void)args; CHECK(0); return 0; }
int kapi_strings_from_source(u32 slot, void *target, struct kapi_arg_source *src, unsigned count, u16 ptrmask, u32 *result) {
    CHECK(slot == 0); (void)target; (void)src; (void)count; (void)ptrmask; (void)result;
    return 0;
}
u32 kapi_invoke(void *fn, const void *args, u32 n) {
    (void)fn; (void)args; (void)n;
    CHECK(caller && ring3_in_syscall && owner==2 && cr3==2 && !jumped);
    invokes++;
    if (request_inside) ring3_abort_request();
    return 0x89abcdef;
}
int kbd_inject_peek_for(int id, u8 *ch) { (void)id; *ch='X'; return 1; }
int kbd_inject_take_for(int id, u8 *ch) { (void)id; *ch='X'; injected++; return 1; }
static int host_if=1;
static u32 irq_save(void) { int old=host_if; host_if=0; return old; }
static void irq_restore(u32 f) { host_if=f; }
static u16 kbd_raw_buf[KBD_BUF_SIZE];
static int kbd_raw_count, kbd_raw_head, kbd_raw_tail;
static u32 kbd_raw_dropped;
static int kbd_gui_mode=1;
static int kbd_stop_suppressed;
static u32 kbd_stop_tick;
#define KBD_STOP_SUPPRESS_TICKS 50
static GuiHandler g_gui_handler;
static void *g_gui_pump;
void ring3_wm_enter(void) { ring3_wm_depth++; }
void ring3_wm_leave(void) { ring3_wm_depth--; }
void kbd_set_gui_mode(int on) { (void)on; }
static void ime_set_render(void *p) { (void)p; }
static i32 notice(u32 op, u32 arg, int id) {
    CHECK(op==GUI_OP_OWNER_EXIT && arg==EXEC_KIND_ABORTED && id==3);
    CHECK(ring3_wm_depth==1); return 0;
}
'''
POST = r'''
static void setup(void) {
    appslot_at(2)->stdin_read.active = 1;
    appslot_init();
    CHECK(!appslot_at(2)->stdin_read.active);
    for (int id=2; id<=3; id++) {
        AppSlot *a=appslot_at(id);
        a->state=APP_STATE_RUNNING; a->gui=1; a->cpl3=1;
        a->parent=1; a->stack_top=MEM_APP_BAND_BASE+4096;
        a->last_kernel_tick=tick_count; a->pages=id*10;
    }
    appslot_switch_to(2); g_cur_app=appslot_get(2); cr3=2;
    ring3_context_clear(); clears=jumped=invokes=injected=aborted=0;
    gui=1; request_inside=0; ring3_stop_park_count=0;
}
static u32 frame[APP_FRAME_WORDS];
static void dispatch_case(int inside) {
    setup(); request_inside=(inside==1);
    for (int i=0;i<APP_FRAME_WORDS;i++) frame[i]=0x12340000+i;
    frame[7]=0; frame[11]=MEM_APP_BAND_BASE;
    if (inside==2) {
        appslot_park_poll_commit();
        ring3_abort_request();
        CHECK(appslot_at(APP_ID_SHELL)->stop_wm_req);
        appslot_resume_commit(2);
        g_cur_app=appslot_get(2); cr3=2;
    }
    if (!inside) ring3_abort_request();
    CHECK(!appslot_get(2)->abort_req);
    if (!exec_setjmp(saved_buf)) { ring3_syscall_dispatch(frame); CHECK(0); }
    AppSlot *a=appslot_get(2);
    CHECK(ring3_stop_park_count==1);
    CHECK(invokes==1 && jumped==1 && clears==1 && g_longjmp_id==2);
    CHECK(!appslot_stop_pending());
    CHECK(g_longjmp_reason==EXEC_LJ_PARK && !a->stop_wm_req && a->parked_from_stop);
    CHECK(a->state==APP_STATE_WAIT_POLL && HOST_FRAME(a->frame)[7]==0x89abcdef);
    CHECK(appslot_stop_culprit()==2);
    CHECK(appslot_stop_culprit()==2 && a->parked_from_stop);
    res_owner_set(3);
    CHECK(appslot_stop_culprit()==OS32_ERR_INVAL);
    res_owner_set(APP_ID_SHELL);
    for(int i=0;i<APP_FRAME_WORDS;i++) CHECK(HOST_FRAME(a->frame)[i]==frame[i]);
    CHECK(!appslot_resume_check(2) && !appslot_kill_check(2));
    CHECK(appslot_resume_source(2)==APP_RESUME_SRC_KEEP);
    CHECK(prepare_resume(2, 999)==0);
    CHECK(HOST_FRAME(a->frame)[7]==0x89abcdef && !injected && invokes==1);
    // target != culprit: caller retains all state and will run again.
    appslot_resume_commit(2);
    CHECK(a->state==APP_STATE_RUNNING && !a->parked_from_stop && !a->stop_wm_req);
    res_owner_set(APP_ID_SHELL);
    CHECK(appslot_stop_culprit()==0);
    CHECK(appslot_get(3)->pages==30 && !appslot_get(3)->abort_req);
}
static void culprit_contract(void) {
    /* Every application ID, including the upper bound; query is read-only. */
    for (int id=APP_ID_MIN; id<=APP_ID_MAX; id++) {
        setup();
        AppSlot *a=appslot_at(id);
        a->state=APP_STATE_RUNNING; a->gui=1; a->cpl3=1;
        appslot_switch_to(id);
        appslot_stop_request();
        appslot_park_stop_commit();
        CHECK(appslot_stop_culprit()==id);
        CHECK(appslot_stop_culprit()==id && a->state==APP_STATE_WAIT_POLL);
        CHECK(!appslot_kill_check(id));
        appslot_reclaim(id); /* exec_kill uses this same reclaim path. */
        CHECK(!a->parked_from_stop && appslot_stop_culprit()==0);
    }
    for (int origin=0; origin<2; origin++) {
        setup();
        if (origin) appslot_park_yield_commit();
        else appslot_park_poll_commit();
        CHECK(appslot_get(2)->state==APP_STATE_WAIT_POLL);
        CHECK(appslot_stop_culprit()==0);
    }
    appslot_init();
    AppSlot *a=appslot_at(APP_ID_MAX);
    a->parked_from_stop=1; /* A mark in a different state is not a culprit. */
    res_owner_set(APP_ID_SHELL);
    CHECK(appslot_stop_culprit()==0);
    a->state=APP_STATE_PARKED;
    CHECK(appslot_stop_culprit()==0);
    for (int id=APP_ID_MIN; id<=APP_ID_MAX; id++) {
        res_owner_set(id);
        CHECK(appslot_stop_culprit()==OS32_ERR_INVAL);
    }
}
static void resume_marks(void) {
    /* Independent cases against appslot.c, never its boot selftest. */
    appslot_init();
    res_owner_set(APP_ID_SHELL);
    AppSlot *a = appslot_at(APP_ID_MAX);
    u32 switches = ring3_switch_count, bad = ring3_resume_bad_frame_count;
    CHECK(appslot_resume_check(APP_ID_MAX) == OS32_ERR_INVAL);
    CHECK(appslot_state(APP_ID_MAX) == 0);
    a->state = APP_STATE_PARKED;
    CHECK(appslot_state(APP_ID_MAX) == 2);
    CHECK(appslot_resume_check(APP_ID_MAX) == OS32_ERR_STALE);
    CHECK(ring3_resume_bad_frame_count == ++bad);
    a->parked_from_wait = 1;
    CHECK(appslot_resume_check(APP_ID_MAX) == 0);
    CHECK(appslot_resume_source(APP_ID_MAX) == APP_RESUME_SRC_WAIT);
    a->state = APP_STATE_WAIT_KEY;
    CHECK(appslot_state(APP_ID_MAX) == 3);
    CHECK(appslot_resume_check(APP_ID_MAX) == OS32_ERR_STALE);
    CHECK(ring3_resume_bad_frame_count == ++bad);
    a->parked_from_kbd = 1;
    CHECK(appslot_resume_check(APP_ID_MAX) == 0);
    CHECK(appslot_resume_source(APP_ID_MAX) == APP_RESUME_SRC_KBD);
    CHECK(appslot_kill_check(APP_ID_MAX) == 0);
    a->state = APP_STATE_RUNNING;
    CHECK(appslot_state(APP_ID_MAX) == 1);
    CHECK(appslot_kill_check(APP_ID_MAX) == OS32_ERR_STALE);
    a->state = APP_STATE_WAIT_POLL;
    CHECK(appslot_state(APP_ID_MAX) == 4);
    /* OP_WAIT and keyboard marks must not authorize polling. */
    CHECK(appslot_resume_check(APP_ID_MAX) == OS32_ERR_STALE);
    CHECK(ring3_resume_bad_frame_count == ++bad);
    a->parked_from_poll = 1;
    CHECK(appslot_resume_check(APP_ID_MAX) == 0);
    CHECK(appslot_kill_check(APP_ID_MAX) == 0);
    CHECK(appslot_resume_source(APP_ID_MAX) == APP_RESUME_SRC_POLL);
    a->parked_from_poll = 0;
    CHECK(appslot_resume_check(APP_ID_MAX) == OS32_ERR_STALE);
    CHECK(ring3_resume_bad_frame_count == ++bad);
    a->parked_from_yield = 1;
    CHECK(appslot_resume_check(APP_ID_MAX) == 0);
    CHECK(appslot_resume_source(APP_ID_MAX) == APP_RESUME_SRC_YIELD);
    a->parked_from_yield = 0;
    a->parked_from_stop = 1;
    CHECK(appslot_resume_check(APP_ID_MAX) == 0);
    CHECK(appslot_kill_check(APP_ID_MAX) == 0);
    CHECK(appslot_resume_source(APP_ID_MAX) == APP_RESUME_SRC_KEEP);
    CHECK(ring3_resume_bad_frame_count == bad);
    CHECK(ring3_switch_count == switches);

    u32 rejects = ring3_park_reject_count, yields = ring3_poll_yield_count;
    g_poll_last_tick = 0x1234;
    CHECK(appslot_park_poll_check(0x1234) == OS32_ERR_AGAIN);
    CHECK(ring3_park_reject_count == rejects);
    CHECK(appslot_park_poll_check(0x1235) == OS32_ERR_INVAL);
    CHECK(ring3_park_reject_count == rejects + 1);
    CHECK(appslot_park_poll_check(0x1235) == OS32_ERR_AGAIN);
    CHECK(ring3_park_reject_count == rejects + 1);
    CHECK(ring3_poll_yield_count == yields);
}
static int park_check_kind(int kind, u32 tick) {
    if(kind==0) return appslot_park_check();
    if(kind==1) return appslot_park_kbd_check();
    if(kind==2) return appslot_park_poll_check(tick);
    return appslot_park_yield_check();
}
static void park_contract(void) {
    for(int kind=0;kind<4;kind++) {
        for(int failure=0;failure<7;failure++) {
            if(failure>=5 && kind!=0) continue;
            setup(); appslot_gui_op_enter(1);
            AppSlot *a=appslot_get(2);
            if(failure==0) appslot_switch_to(APP_ID_SHELL);
            if(failure==1) g_cur=APP_ID_MAX+1;
            if(failure==2) a->state=APP_STATE_FREE;
            if(failure==3) a->state=APP_STATE_PARKED;
            if(failure==4) a->gui=0;
            if(failure==5) g_cur_op_is_wait=0;
            if(failure==6) a->in_op_wait=0;
            u32 rejects=ring3_park_reject_count;
            CHECK(park_check_kind(kind,1)==OS32_ERR_INVAL);
            CHECK(ring3_park_reject_count==rejects+1);
            if(kind==2) {
                CHECK(park_check_kind(kind,1)==OS32_ERR_AGAIN);
                CHECK(ring3_park_reject_count==rejects+1);
            }
        }
        setup(); appslot_gui_op_enter(1);
        AppSlot *a=appslot_get(2);
        a->stop_wm_req=appslot_at(APP_ID_SHELL)->stop_wm_req=1;
        u32 rejects=ring3_park_reject_count, kbd=ring3_kbd_park_count;
        u32 poll=ring3_poll_yield_count, yield=ring3_yield_count;
        CHECK(park_check_kind(kind,1)==0);
        if(kind!=2) CHECK(park_check_kind(kind,1)==0);
        if(kind==0) appslot_park_commit();
        if(kind==1) appslot_park_kbd_commit();
        if(kind==2) appslot_park_poll_commit();
        if(kind==3) appslot_park_yield_commit();
        CHECK(a->parked_from_wait==(kind==0) && a->parked_from_kbd==(kind==1));
        CHECK(a->parked_from_poll==(kind==2) && a->parked_from_yield==(kind==3));
        CHECK(!a->parked_from_stop && !a->stop_wm_req && !appslot_at(1)->stop_wm_req);
        CHECK(a->state==(kind==0 ? APP_STATE_PARKED :
                        kind==1 ? APP_STATE_WAIT_KEY : APP_STATE_WAIT_POLL));
        CHECK(!a->in_op_wait && !g_cur_op_is_wait && appslot_cur()==1 && owner==1);
        CHECK(ring3_kbd_park_count==kbd+(kind==1));
        CHECK(ring3_poll_yield_count==poll+(kind==2));
        CHECK(ring3_yield_count==yield+(kind==3));
        CHECK(ring3_park_reject_count==rejects);
    }
}
static int test(void) {
    static struct addrspace as;
    (void)as;
    culprit_contract();
    park_contract();
    dispatch_case(0); dispatch_case(1); dispatch_case(2);
    setup(); gui=0; ring3_abort_request(); CHECK(appslot_get(2)->abort_req && !appslot_get(2)->stop_wm_req);
    ring3_abort_check(); ring3_abort_check(); CHECK(aborted==1 && ring3_abort_count==1);
    setup(); tick_count+=APP_RUNAWAY_TICKS; ring3_abort_request();
    CHECK(appslot_get(2)->abort_req && !appslot_get(2)->stop_wm_req);
    setup(); tick_count=1; appslot_get(2)->last_kernel_tick=0xfffffffe;
    ring3_abort_request(); CHECK(appslot_get(2)->stop_wm_req && !appslot_get(2)->abort_req);
    for(int kind=0;kind<4;kind++) {
        setup(); ring3_abort_request();
        if(kind==0) { appslot_gui_op_enter(1); appslot_park_commit(); }
        if(kind==1) appslot_park_kbd_commit();
        if(kind==2) appslot_park_poll_commit();
        if(kind==3) appslot_park_yield_commit();
        CHECK(!appslot_get(2)->stop_wm_req && !appslot_get(2)->parked_from_stop);
    }
    setup(); ring3_abort_request(); res_owner_set(1); appslot_abort_clear();
    CHECK(!appslot_get(2)->stop_wm_req);
    setup(); ring3_abort_request();
    appslot_at(2)->stdin_read.active = 1;
    appslot_at(2)->stdin_read.done = 2;
    appslot_reclaim(2);
    CHECK(!appslot_at(2)->stdin_read.active && !appslot_at(2)->stdin_read.done);
    CHECK(!appslot_at(2)->stop_wm_req && !appslot_at(2)->parked_from_stop);
    // Refusal guards: no unsafe-point transfer.
    for(int guard=0;guard<6;guard++) {
        setup(); ring3_abort_request();
        if(guard==0) kctx_irq_depth=1;
        if(guard==1) kctx_exc_depth=1;
        if(guard==2) ring3_wm_depth=1;
        if(guard==3) appslot_get(2)->in_op_wait=1;
        if(guard==4) appslot_get(2)->cpl3=0;
        if(guard==5) g_cur_app=appslot_get(3);
        struct arch_syscall call = { frame };
        exec_park_stop(&call); CHECK(!jumped && cr3==2 && owner==2);
        kctx_irq_depth=kctx_exc_depth=ring3_wm_depth=0;
    }
    setup(); appslot_get(2)->gui=0; appslot_stop_request(); CHECK(!appslot_get(2)->stop_wm_req);
    appslot_switch_to(1); appslot_stop_request(); CHECK(appslot_stop_pending());
    appslot_abort_clear(); CHECK(!appslot_stop_pending());
    setup(); appslot_get(2)->cpl3=0; appslot_stop_request();
    CHECK(!appslot_get(2)->stop_wm_req);
    setup(); appslot_get(2)->in_op_wait=1; ring3_abort_request();
    CHECK(appslot_get(2)->abort_req && !appslot_get(2)->stop_wm_req);
    ring3_abort_check(); CHECK(aborted==1);
    setup(); appslot_at(2)->stop_wm_req=1; appslot_start_commit(2,1,20);
    CHECK(!appslot_at(2)->stop_wm_req);
    setup(); appslot_at(2)->stop_wm_req=1; appslot_resume_commit(2);
    CHECK(!appslot_at(2)->stop_wm_req);
    for(int kind=0;kind<6;kind++) {
        setup(); appslot_at(1)->stop_wm_req=1;
        if(kind==0) { appslot_gui_op_enter(1); appslot_park_commit(); }
        if(kind==1) appslot_park_kbd_commit();
        if(kind==2) appslot_park_poll_commit();
        if(kind==3) appslot_park_yield_commit();
        if(kind==4) { res_owner_set(1); appslot_abort_clear(); }
        if(kind==5) appslot_reclaim(2);
        CHECK(!appslot_at(1)->stop_wm_req);
    }
    // Unread raw STOP across ring wrap: preserve all other input and IF.
    u16 stop=KEY_STOP|0x100|(SHIFT_CTRL<<9);
    kbd_raw_head=KBD_BUF_SIZE-2; kbd_raw_count=5; kbd_raw_tail=3;
    kbd_raw_buf[KBD_BUF_SIZE-2]=stop; kbd_raw_buf[KBD_BUF_SIZE-1]=0x123;
    kbd_raw_buf[0]=stop; kbd_raw_buf[1]=stop & ~0x100;
    kbd_raw_buf[2]=KEY_STOP|0x100; /* STOP without CTRL survives. */
    kbd_discard_stop();
    CHECK(host_if && kbd_raw_count==3 && kbd_raw_tail==1);
    CHECK(kbd_raw_buf[0]==(KEY_STOP|0x100));
    CHECK(kbd_raw_buf[KBD_BUF_SIZE-2]==0x123 && kbd_raw_buf[KBD_BUF_SIZE-1]==(stop & ~0x100));
    for(int head=0;head<KBD_BUF_SIZE;head++) {
        kbd_raw_head=kbd_raw_tail=head; kbd_raw_count=kbd_raw_dropped=0;
        for(int i=0;i<KBD_BUF_SIZE;i++) raw_enqueue(i,0,0);
        raw_enqueue(42,0,0);
        CHECK(kbd_raw_count==KBD_BUF_SIZE && kbd_raw_dropped==1);
        // None of these full-ring events may replace the newest entry.
        const int keys[3]={KEY_STOP,KEY_STOP,KEY_CTRL};
        const int breaks[3]={0,1,0};
        const int mods[3]={0,SHIFT_CTRL,SHIFT_CTRL};
        for(int k=0;k<3;k++) {
            raw_enqueue(keys[k],breaks[k],mods[k]);
            CHECK(kbd_raw_count==KBD_BUF_SIZE && kbd_raw_dropped==(u32)(k+2));
            for(int i=0;i<KBD_BUF_SIZE;i++) CHECK(kbd_raw_buf[(head+i)%KBD_BUF_SIZE]==(i|0x100));
        }
        raw_enqueue(KEY_STOP,0,SHIFT_CTRL);
        CHECK(kbd_raw_count==KBD_BUF_SIZE && kbd_raw_dropped==5);
        for(int i=0;i<KBD_BUF_SIZE-1;i++) CHECK(kbd_raw_buf[(head+i)%KBD_BUF_SIZE]==(i|0x100));
        CHECK(kbd_raw_buf[(head+KBD_BUF_SIZE-1)%KBD_BUF_SIZE]==stop);
    }
    g_gui_handler=notice; gui_owner_exit(3, EXEC_KIND_ABORTED);
    CHECK(!ring3_wm_depth);
    resume_marks();
    return 0;
}
void _start(void) { int r=test(); __asm__ volatile("int $0x80"::"a"(1),"b"(r):"memory"); for(;;){} }
'''

def run(overrides=None):
    overrides = overrides or {}
    def read(p):
        return overrides.get(p, (ROOT / p).read_text())
    ex = exec_source(ROOT, read)
    resume = function(ex, 'exec_resume')
    resume = resume[:resume.index('    volatile int x87_depth')] + '    return 0;\n}'
    # Preparation alone: the existing R1 harness exercises the real landing.
    resume = resume.replace('exec_resume(', 'prepare_resume(').replace('!a->as->pd_phys', '0')
    import re
    constants = ''
    for rust, rname, cname in [('handler','OWNER_EXIT_ABORTED','EXEC_KIND_ABORTED'),
                              ('input','SC_STOP','KEY_STOP'), ('input','MOD_CTRL','SHIFT_CTRL')]:
        value=re.search(r'const '+rname+r': \w+ = (0x[0-9a-fA-F]+|[0-9]+);',read('userland/gshell/src/'+rust+'.rs'))[1]
        constants += '_Static_assert('+cname+' == '+value+', "'+rname+'");\n'
    transition = read('exec/appslot_transition.inc').replace(
        '#include "appslot_park.inc"', read('exec/appslot_park.inc'))
    hwb_flag = re.search(r'^static volatile u32 g_hwb_abort_pending = 0;', ex, re.M)[0]
    hwb_flag += '\n' + re.search(r'^static volatile u32 g_cui_display_abort_pending;', ex, re.M)[0]
    code = PRE + constants + hwb_flag + '\n' + read('exec/appslot.c').replace(
        '#include "appslot_transition.inc"', transition) + '\n' + syscall_providers(ROOT, read) + '\n'.join(function(ex,n) for n in (
        'exec_cui_display_abort','ring3_abort_request','ring3_abort_check','exec_park_transfer','exec_park_stop','exec_syscall_dispatch')) + '\n' + resume + '\n'
    kbd = read('drivers/input/kbd.c')
    deliver = function(kbd, 'kbd_deliver')
    raw = deliver[deliver.index('    if (kbd_gui_mode) {'):deliver.index('    if (is_mod) return;')]
    code += 'static void raw_enqueue(int keycode, int is_break, int kbd_shift_state) {\n' + raw + '}\n'
    code += function(kbd, 'kbd_discard_stop') + '\n'
    code += function(read('kernel/gui.c'), 'gui_owner_exit') + '\n'
    code += read('kernel/input_service.c') + '\nstatic InputOps kstop_input;\n'
    code += POST.replace('void _start(void) {', 'void _start(void) { kstop_input = empty_input; kstop_input.gui_mode = kbd_set_gui_mode; input_register(&kstop_input);')
    with tempfile.TemporaryDirectory(prefix='os32-kstop-') as d:
        d=pathlib.Path(d)
        (d/'provider.c').write_text(read('arch/x86/kernel/exec_context.c'))
        code = '#define HOST_EXEC_CONTEXT_SOURCE "' + str(d/'provider.c') + '"\n' + code
        (d/'t.c').write_text(code)
        (d/'s.asm').write_text((ROOT/'arch/x86/kernel/setjmp.asm').read_text().replace('exec_longjmp','asm_longjmp'))
        subprocess.run(['nasm','-f','elf32',str(d/'s.asm'),'-o',str(d/'s.o')],check=True,capture_output=True)
        inc=(['-I' + str(ROOT / p) for p in ('tools/tests', 'tools/tests/host_arch')] + include_flags(ROOT))
        r=subprocess.run(['gcc','-m32','-std=gnu11','-O0','-Wall','-Werror','-Wno-unused-function',
          '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-ffreestanding','-fno-pie',
          '-fno-stack-protector','-nostdlib','-static','-no-pie',*inc,str(d/'t.c'),str(d/'s.o'),'-o',str(d/'t')],capture_output=True,text=True)
        if r.returncode: return 'compile',r.stderr
        r=host32.run([str(d/'t')],capture_output=True,text=True,timeout=30)
        return ('pass' if r.returncode==0 else 'runtime'),r.stdout+r.stderr

MUTATIONS = [
 ('provider-syscall-result', 'arch/x86/kernel/exec_context.c',
  '    ((u32 *)call->native)[FRAME_EAX] = result;',
  '    ((u32 *)call->native)[FRAME_EAX] = result + 1;'),
 ('provider-save-frame', 'arch/x86/kernel/exec_context.c',
  '    __builtin_memcpy(out->opaque, call->native, sizeof(out->opaque));',
  '    __builtin_memcpy(out->opaque, call->native, sizeof(out->opaque) - sizeof(u32));'),
 ('culprit-ignore-mark','exec/appslot_transition.inc',
  'a && a->state == APP_STATE_WAIT_POLL && a->parked_from_stop)',
  'a && a->state == APP_STATE_WAIT_POLL)'),
 ('culprit-ignore-state','exec/appslot_transition.inc',
  'a && a->state == APP_STATE_WAIT_POLL && a->parked_from_stop)',
  'a && a->parked_from_stop)'),
 ('culprit-ignore-owner','exec/appslot_transition.inc',
  'int appslot_stop_culprit(void)\n{\n    if (res_owner_get() != APP_ID_SHELL) return OS32_ERR_INVAL;',
  'int appslot_stop_culprit(void)\n{'),
 ('lost-shell-request','exec/appslot_transition.inc','        g_slot[APP_ID_SHELL].stop_wm_req = 1;','        g_slot[APP_ID_SHELL].stop_wm_req = 0;'),
 ('ignore-shell-request','exec/appslot_transition.inc','    return g_slot[APP_ID_SHELL].stop_wm_req || (a && a->stop_wm_req);','    return a && a->stop_wm_req;'),
 ('lost-notice-kind','kernel/gui.c','g_gui_handler(GUI_OP_OWNER_EXIT, (u32)kind, owner);','g_gui_handler(GUI_OP_OWNER_EXIT, 0, owner);'),
 ('no-request','exec/exec_abort.inc','        appslot_stop_request();','        /* missing request */'),
 ('no-safe-transfer','exec/exec_syscall.inc','    exec_park_stop(&call);','    /* missing transfer */'),
 ('before-completion','exec/exec_syscall.inc','    result = arch_kapi_invoke','    exec_park_stop(&call);\n    result = arch_kapi_invoke'),
 ('eax-clobber','exec/exec_schedule.inc','        /* STOP parked a completed syscall;', '        arch_user_result(&a->frame, (u32)wait_ret);\n        /* STOP parked a completed syscall;'),
 ('wrong-owner','exec/appslot_transition.inc','    a->parked_from_stop = 1;', '    g_slot[3].abort_req = 1;\n    a->parked_from_stop = 1;'),
 ('stale-request','exec/appslot_transition.inc','    a->stop_wm_req = 0;\n    g_slot[APP_ID_SHELL].stop_wm_req = 0;\n    ring3_stop_park_count++;\n    a->parked_from_stop = 1;', '    a->parked_from_stop = 1;'),
 ('no-mark','exec/appslot_transition.inc','    a->parked_from_stop = 1;', '    a->parked_from_stop = 0;'),
 ('raw-stop-retained','drivers/input/kbd.c','(raw & (SHIFT_CTRL << KBD_RAW_MOD_SHIFT))) continue;', '(raw & (SHIFT_CTRL << KBD_RAW_MOD_SHIFT))) { /* retain */ }'),
]
# Function-scoped replacements, each with exactly one match.
for guard in ('!a->cpl3 ||', 'a != g_cur_app ||', 'a->in_op_wait ||',
              'ring3_wm_depth ||', 'kctx_irq_depth ||', 'kctx_exc_depth'):
    src = function(exec_source(ROOT), 'exec_park_stop')
    assert src.count(guard)==1
    MUTATIONS.append(('guard-'+guard, 'exec/exec_syscall.inc', src, src.replace(guard, '0' if guard=='kctx_exc_depth' else '')))
for name, origin in (('appslot_park_commit', 'wait'), ('appslot_park_kbd_commit', 'kbd'),
                     ('appslot_park_poll_commit', 'poll'), ('appslot_park_yield_commit', 'yield')):
    path = 'exec/appslot_park.inc'
    src = function((ROOT/path).read_text(), 'appslot_park_commit_origin')
    for line in src.splitlines():
        if 'stop_wm_req = 0;' in line:
            assert src.count(line)==1
            # Preserve the separate mutant for each entry's request clearing.
            conditional = f'    if (origin != &a->parked_from_{origin}) {line.strip()}'
            MUTATIONS.append((name+'-'+line.strip(), path, src, src.replace(line, conditional)))
for name in ('appslot_abort_clear','appslot_start_commit','appslot_resume_commit'):
    path = 'exec/appslot.c' if name in ('appslot_abort_clear', 'appslot_start_commit') else 'exec/appslot_transition.inc'
    src = function((ROOT/path).read_text(),name)
    for line in src.splitlines():
        if 'stop_wm_req = 0;' in line:
            assert src.count(line)==1
            MUTATIONS.append((name+'-'+line.strip(), path,src,src.replace(line,'')))
src = (ROOT/'exec/appslot.c').read_text()
a=src.index('u32 appslot_reclaim('); b=src.index('\n}',a)+2
src=src[a:b]
MUTATIONS.append(('reclaim-shell-request','exec/appslot.c',src,src.replace('    g_slot[APP_ID_SHELL].stop_wm_req = 0;','')))
src=function((ROOT/'exec/appslot_transition.inc').read_text(),'appslot_stop_request')
MUTATIONS.append(('request-non-gui','exec/appslot_transition.inc',src,src.replace('a->gui && ','')))
MUTATIONS.append(('raw-full-loses-stop','drivers/input/kbd.c',
    'if (keycode == KEY_STOP && !is_break && (kbd_shift_state & SHIFT_CTRL)) {',
    'if (0) {'))

for name, guard in (('raw-full-replaces-break', '!is_break && '),
                    ('raw-full-replaces-without-ctrl', ' && (kbd_shift_state & SHIFT_CTRL)')):
    old = 'if (keycode == KEY_STOP && !is_break && (kbd_shift_state & SHIFT_CTRL)) {'
    assert old.count(guard) == 1
    MUTATIONS.append((name, 'drivers/input/kbd.c', old, old.replace(guard, '')))

# Resume-mark cases must catch regressions without the boot selftest.
for name, fn, old, new in (
    ('wait-mark', 'appslot_resume_check', '!a->parked_from_wait', '0'),
    ('kbd-mark', 'appslot_resume_check', '!a->parked_from_kbd', '0'),
    ('poll-mark', 'appslot_resume_check', '!a->parked_from_poll &&', '0 &&'),
    ('bad-frame-count', 'appslot_resume_check', 'ring3_resume_bad_frame_count++;', ';'),
    ('refusal-switch', 'appslot_resume_check', 'AppSlot *a;', 'AppSlot *a; ring3_switch_count++;'),
    ('yield-source', 'appslot_resume_source', 'return APP_RESUME_SRC_YIELD;', 'return APP_RESUME_SRC_POLL;'),
    ('stop-source', 'appslot_resume_source', 'return APP_RESUME_SRC_KEEP;', 'return APP_RESUME_SRC_POLL;'),
    ('poll-throttle', 'appslot_park_poll_check', 'now_tick == g_poll_last_tick', '0'),
    ('poll-attempt-tick', 'appslot_park_poll_check', 'g_poll_last_tick = now_tick;', ';'),
):
    src = function((ROOT/'exec/appslot_transition.inc').read_text(), fn)
    assert old in src
    MUTATIONS.append((name, 'exec/appslot_transition.inc', src, src.replace(old, new)))

for name, fn, old, new in (
    ('wait-op-gate', 'appslot_park_check', '!g_cur_op_is_wait ||', '0 ||'),
    ('wait-slot-gate', 'appslot_park_check', '!a->in_op_wait', '((void)a->in_op_wait, 0)'),
    ('park-running', 'appslot_park_kbd_check', 'a->state != APP_STATE_RUNNING', '0'),
    ('park-gui', 'appslot_park_kbd_check', '!a->gui', '0'),
    ('park-reject-count', 'appslot_park_kbd_check', 'ring3_park_reject_count++;', ';'),
    ('kbd-park-count', 'appslot_park_kbd_commit', 'ring3_kbd_park_count++;', ';'),
    ('poll-yield-count', 'appslot_park_poll_commit', 'ring3_poll_yield_count++;', ';'),
    ('yield-count', 'appslot_park_yield_commit', 'ring3_yield_count++;', ';'),
):
    src = function((ROOT/'exec/appslot_transition.inc').read_text(), fn)
    assert old in src
    MUTATIONS.append((name, 'exec/appslot_transition.inc', src, src.replace(old, new)))
for origin in ('wait', 'kbd', 'poll', 'yield'):
    old = f'&a->parked_from_{origin},'
    new = '&a->parked_from_stop,'
    MUTATIONS.append((origin+'-park-origin', 'exec/appslot_transition.inc', old, new))

if __name__ == '__main__':
    status,log=run(); print('kstop control:',status,log,flush=True)
    if status!='pass': sys.exit(1)
    if '--mutate' in sys.argv:
        for name,p,old,new in MUTATIONS:
            source=(ROOT/p).read_text(); assert source.count(old)==1,(name,source.count(old))
            status,log=run({p:source.replace(old,new)})
            print(name,status,log,flush=True)
            if status!='runtime': sys.exit(1)
        print(f'kstop: {len(MUTATIONS)}/{len(MUTATIONS)} runtime RED')
