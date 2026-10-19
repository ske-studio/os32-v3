/* Real recorder + session begin/end + exec transfer/landing. CPU mappings,
 * ROM execution and IRQ arrival are substitutes, never a simulated IDT stack. */
#include "types.h"
static u32 host_cr3;
#include "v86.h"
#include "v86_mem.h"
#include "v86_gcap.h"
#include "v86_gcap_math.h"
#include "module.h"
#include "pgalloc.h"
#include "appslot.h"
#include "exec.h"
#include "io.h"
#include "idt.h"
#include "gfx.h"
#include "gfx_hal.h"
#include "console.h"
#include "kstring.h"
#include "kmalloc.h"
#include "v86_call.h"

static void die(int rc) __attribute__((noreturn));
static void die(int rc) { __asm__ volatile("int $0x80"::"a"(1),"b"(rc)); for (;;) {} }
static void say(const char *s, u32 n) { __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory"); }
#define SAY(s) say(s "\n", sizeof(s "\n") - 1)
#define CHECK(c, s) do { if (!(c)) { SAY("FAIL " s); die(1); } } while (0)
volatile u32 kctx_irq_depth, kctx_exc_depth;
struct kdebug kdebug;
struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
u32 v86_gcap_phase, v86_gcap_alloc_count, v86_gcap_free_count, v86_gp_cs, v86_gp_ip;
static u32 step, inject_step, injection, failure_step, calls, killed[5], landed[5];
static u32 refs, allocations, frees, policy_deny, display_reset, cursor, finishes, sync_restores;
static u32 fault_kill_count, ring3_abort_count;
volatile u32 ring3_wm_fault_count, tick_count;
volatile int ring3_wm_depth;
static int g_cur_app = 1;
static u32 g_exit_jmpbuf[KSETJMP_BUF_LEN];
static int g_pending_id, g_pending_kind, g_longjmp_reason, g_longjmp_id;
#define EXEC_LJ_EXIT 5
#define EXEC_LJ_PENDING 4
static AppSlot app;
static struct { int pinned; u32 cr3; void *path[2]; V86Gcap *capture; } call;
static V86Gcap record_storage;
static u16 tv_storage[TVRAM_COLS * TVRAM_ROWS_30 * 2];
static void boundary(void);
static void exec_stop_mark(void) { CHECK(0, "unexpected teardown stop"); }
static void ring3_context_clear(void) { kctx_irq_depth = kctx_exc_depth = 0; }
int appslot_cur(void) { return 2; }
AppSlot *appslot_get(int id) { (void)id; return &app; }
int appslot_abort_admit(int gui, u32 tick) { (void)gui; (void)tick; return 1; }
int appslot_abort_request(void) { app.abort_req = 1; return 1; }
void appslot_stop_request(void) { CHECK(0, "STOP accepted"); }
int con_sink_is_enabled(void) { return 0; }
static void exec_finish(int id, int status, int kind) {
    (void)id; (void)status; (void)kind;
    CHECK(!call.pinned && allocations == frees, "release before owner reclaim"); finishes++;
}
void exec_longjmp(u32 *buf) {
    if (buf == g_exit_jmpbuf) __builtin_longjmp(landed, 1);
    CHECK(!v86_session.open && !v86_gcap_active(), "exception detach before transfer");
    CHECK(allocations != frees, "no allocator on exception stack");
    __builtin_longjmp(killed, 1);
}
#include "cleanup.inc"
#include "kill.inc"
static void boundary(void) {
    step++;
    if (step != inject_step) return;
    CHECK(v86_gcap_active() || (!v86_session.open && allocations > frees), "observe capture before injection");
    if (injection == 3) {
        kctx_irq_depth = 1;
        ring3_abort_request();
        ring3_abort_check();
    } else {
        kctx_exc_depth = 1; /* #PF / #GP decoder's existing kill edge. */
        ring3_fault_kill();
    }
    CHECK(0, "kill does not return");
}
void *kmemcpy(void *d, const void *s, u32 n) { u8 *x=d; const u8 *y=s; while(n--) *x++=*y++; return d; }
void *kmemset(void *d, int c, u32 n) { u8 *x=d; while(n--) *x++=(u8)c; return d; }
void *kzalloc(u32 n) { CHECK(n == sizeof(record_storage), "g size"); allocations++; return kmemset(&record_storage, 0, n); }
void *kmalloc(u32 n) { CHECK(n == sizeof(tv_storage), "tv size"); allocations++; return tv_storage; }
void kfree(void *p) {
    CHECK(p == &record_storage || p == tv_storage, "g/tv ownership");
    CHECK(!kctx_irq_depth && !kctx_exc_depth, "free on normal stack only"); frees++;
}
u32 exec_ledger_owner(void) { return 2; }
int v86_gui_refuse(void) { return 0; }
int v86_is_active(void) { return v86_session.active; }
int gfx_get_height(void) { return GFX_HEIGHT; }
void console_hw_cursor_enable(void) { cursor++; }
void v86_cui_display_restore(void) {}
void __attribute__((weak)) pegc_restore_text_sync(int freq) { CHECK(freq == 1, "saved ROM sync frequency"); sync_restores++; }
int serial_gate_active(void) { return 1; }
void serial_puts_polled(const char *s) { (void)s; }
void v86_pic_set_imr(int slave, u8 mask) { (void)slave; (void)mask; }
void paging_load_cr3(u32 root) { host_cr3 = root; }
u32 paging_kernel_pd_phys(void) { return 0x200000; }
int v86_module_session_acquire(void) { boundary(); if(step == failure_step) return 0; refs++; return 1; }
void v86_module_session_release(void) { if (!refs) return; CHECK(refs == 1, "module release once"); refs--; }
int paging_v86_snapshot(struct v86_session_state *s) {
    boundary();
    for(u32 a=0; a<V86_GUEST_MAP_END; a+=PAGE_SIZE)
        s->low_pte[a/PAGE_SIZE] =
            ((a>=V86_VRAM_START && a<TVRAM_CG_WINDOW) ||
             (a>=GVRAM_PLANE_B && a<V86_VRAM_END) ||
             (a>=V86_GVRAM_E_START && a<V86_GVRAM_E_END)) ? PTE_PCD : 0;
    return step == failure_step ? -1 : 0;
}
int paging_v86_map_range(u32 a, u32 end, u32 p, u32 f) {
    (void)a; (void)end; (void)p; (void)f; boundary(); return step == failure_step ? -1 : 0;
}
int paging_v86_restore(struct v86_session_state *s) { (void)s; return 0; }
u32 pgalloc_alloc_phys(u32 owner, int n) { (void)owner; (void)n; boundary(); return step == failure_step ? 0 : 0x300000; }
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n) { (void)owner; (void)pfn; (void)n; return 0; }
void v86_bios_setup(void) { boundary(); }
void v86_bios_restore_real(void) {}
void v86_bios_detach_disk(void) {}
void v86_runtime_end(void) { v86_session.active = v86_session.running = 0; }
void gfx_v86_return(void) {}
void kselftest_audit_v86_return(void) {}
static const struct kdebug_ops *v86_debug_ops(void) { return &kdebug.ops; }
static void tss_iomap_deny_all(void) { policy_deny++; }
static void gfx_state_for_os32(void) { display_reset++; }
static void snd_state_for_os32(void) {}
#include "policy.inc"
void v86_io_apply_policy(void) { policy_deny = 0; boundary(); }
static void restore_stop(void) { CHECK(0, "unexpected restore stop"); }
#define _stop restore_stop
#include "session.c"
#undef _stop
/* First boundary precedes opening the low mapping. Last boundary follows its
 * normal teardown but precedes recorder copyout/release. */
static int checked_setup(u32 owner) { boundary(); return v86_mem_setup(owner); }
static void checked_end(void) { v86_mem_teardown(); boundary(); }
#define v86_mem_setup checked_setup
#define v86_mem_teardown checked_end
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
#include "recorder.c"
#pragma GCC diagnostic pop
#undef v86_mem_setup
#undef v86_mem_teardown
#include "facade.c"
int v86_run_limit(const struct v86_context *ctx, u32 ticks) {
    (void)ctx; (void)ticks;
    boundary(); calls++;
    /* ROM replies permit READ31 -> SET480 -> RESTORE. Recorder selftest
     * contents already have a dedicated suite; it still takes all five runs. */
    gd_set(GD_OUT_AX, v86_gcap_phase == V86G_PH_READ31 ? 0x310d : 0x0500);
    gd_set(GD_OUT_BX, 0x0100);
    gd_set(GD_DONE, GD_DONE_MARK);
    for(u32 i=0; record_storage.mode == V86G_MODE_ROM && i<GCAP_TV_CELLS; i++) {
        ((volatile u16 *)P2V_IO(TVRAM_CHAR_BASE))[i] = 0xdead;
        ((volatile u16 *)P2V_IO(TVRAM_ATTR_BASE))[i] = 0xbeef;
    }
    return V86_EXIT_HLT;
}
static u16 original(u32 i) { return (u16)(i ^ 0x1234); }
static void before(void) {
    step = calls = policy_deny = display_reset = cursor = sync_restores = 0;
    call.pinned = 1; call.cr3 = 0x400000; host_cr3 = paging_kernel_pd_phys();
    v86_session.aborting = 0;
    for(u32 i=0; i<GCAP_TV_CELLS; i++) {
        ((volatile u16 *)P2V_IO(TVRAM_CHAR_BASE))[i] = original(i);
        ((volatile u16 *)P2V_IO(TVRAM_ATTR_BASE))[i] = original(i+GCAP_TV_CELLS);
    }
    /* Guest IVT checked by the genuine gcap_rom. */
    *(volatile u32 *)P2V(0x18 * 4) = 0xf0000000;
}
static void after(void) {
    CHECK(!v86_gcap_active() && !gcap_ops && !gcap_keep_if, "active ops keep_if cleared");
    CHECK(!gcap_owned && !gcap_tv && allocations == frees &&
          v86_gcap_alloc_count == v86_gcap_free_count, "g/tv alloc-free zero");
    CHECK(!refs && !v86_session.open && !v86_session.release_pending, "session released");
    CHECK(!call.pinned && host_cr3 == call.cr3, "caller CR3 unpin");
    for(u32 i=0; i<GCAP_TV_CELLS; i++) {
        CHECK(((volatile u16 *)P2V_IO(TVRAM_CHAR_BASE))[i] == original(i), "TVRAM 30 rows chars");
        CHECK(((volatile u16 *)P2V_IO(TVRAM_ATTR_BASE))[i] == original(i+GCAP_TV_CELLS), "TVRAM 30 rows attrs");
    }
}
void _start(void) {
    u32 args[6] = {0, 16*1024*1024, 3, 0x32, ~0U, 0}, result;
    __asm__ volatile("int $0x80":"=a"(result):"a"(90),"b"(args):"memory");
    CHECK(!result, "low fixture mapping");
    kdebug.ops.gcap_capture = capture_body;
    kdebug.ops.gcap_active = v86_gcap_active;
    kdebug.ops.gcap_detach = v86_gcap_detach;
    kdebug.ops.gcap_release = v86_gcap_release;
    static V86Gcap output;
    /* KDEBUG OFF facade still returns NOSYS without starting anything. */
    kdebug.ops.gcap_capture = 0;
    CHECK(v86_gdc_capture(V86G_MODE_ROM, &output) == OS32_ERR_NOSYS, "KDEBUG OFF");
    kdebug.ops.gcap_capture = capture_body;
    for(int mode=V86G_MODE_ROM; mode<=V86G_MODE_SELFTEST; mode++) {
        before();
        CHECK(v86_gdc_capture(mode, &output) >= 0, "normal capture");
        u32 total = step;
        v86_call_cleanup();
        if(mode == V86G_MODE_SELFTEST)
            CHECK(!cursor && !display_reset, "normal dry capture leaves display alone");
        after();
        for(u32 entry=1; entry<=total; entry++) {
            for(injection=1; injection<=3; injection++) {
                before(); inject_step = entry;
                u32 kills_before = fault_kill_count, aborts_before = ring3_abort_count;
                if(!__builtin_setjmp(killed)) {
                    v86_gdc_capture(mode, &output);
                    CHECK(0, "injection reached");
                }
                kctx_irq_depth = kctx_exc_depth = 0; /* trusted setjmp landing */
                exec_pending_finish();
                CHECK(fault_kill_count == kills_before+1, "kills delta one");
                CHECK(ring3_abort_count == aborts_before+(injection==3), "STOP observed active");
                CHECK(policy_deny && display_reset && cursor, "output policy and display restored");
                if(mode == V86G_MODE_ROM && (entry == 17 || entry == 18))
                    CHECK(sync_restores == 1, "interrupted ROM mode restores sync");
                after(); inject_step = 0;
                before();
                CHECK(v86_gdc_capture(V86G_MODE_ROM, &output) >= 0 && calls == 3, "next V86 succeeds");
                v86_call_cleanup(); after();
            }
        }
        /* Recoverable preparation failures at module, snapshot, backing and
         * every mapping boundary (steps 2..13); all retain caller output. */
        for(failure_step=2; failure_step<=13; failure_step++) {
            before(); CHECK(v86_gdc_capture(mode, &output) == V86G_ST_SETUP, "prepare failure status");
            v86_call_cleanup(); after();
        }
        failure_step = 0;
    }
    CHECK(finishes > 0, "kill landing exercised");
    SAY("GREEN capture active STOP/#PF/#GP at every entry / prepare failure / release / TVRAM30 / CR3 / next V86 / KDEBUG OFF");
    die(0);
}
