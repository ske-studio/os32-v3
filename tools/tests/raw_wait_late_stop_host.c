/* Real exec_finish + AppSlot; inject IRQ1's request during resource/page
 * teardown. Device cleanup and CR3 changes are recording seams. */
#include "appslot.h"
#include "exec.h"
#include "fd_redirect.h"

static void say(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(c, message) do { if (!(c)) { say("FAIL late STOP: " message "\n"); return 1; } } while (0)
static int owner, inject_at, resources, teardown, notices, injections, bad;
static int kills, exec_exit_status, g_last_kind, g_last_code;
volatile int exec_nest_level;
static u32 g_exit_jmpbuf[KSETJMP_BUF_LEN], tick_count;
static AppSlot *g_cur_app;
volatile u32 ring3_abort_count;

void res_owner_set(int id) { owner = id; }
int res_owner_get(void) { return owner; }
void fd_redirect_save(FdRedirectState *p) { (void)p; }
void fd_redirect_restore(const FdRedirectState *p) { (void)p; }
void fd_redirect_clear_state(FdRedirectState *p) { (void)p; }
static int con_sink_is_enabled(void) { return 0; }
static void ring3_abort_kill(void) { kills++; }
u32 paging_kernel_pd_phys(void) { return 1; }
void paging_load_cr3(u32 p) { if (p != 1) bad++; }
int paging_set_page(u32 a, u32 b, u32 c) { (void)a; (void)b; (void)c; bad++; return 0; }
static void exec_heap_reset(void) { bad++; }
static void ring3_band_set(int n) { (void)n; bad++; }
static void exec_reclaim_owned(int id, int kind) { (void)id; (void)kind; bad++; }
void ring3_abort_request(void);

static void inject(int phase)
{
    if (phase != inject_at) return;
    if (appslot_cur() != 3 || owner != 3 || !appslot_get(3) ||
        appslot_get(3)->abort_req || appslot_get(2)->abort_req) bad++;
    ring3_abort_request(); /* IRQ request only: CPL0 IRQ return cannot kill. */
    if (!appslot_get(3)->abort_req || appslot_get(2)->abort_req || kills) bad++;
    injections++;
}
static void exec_reclaim_resources(int id)
{ if (id != 3) bad++; resources++; inject(1); }
static void exec_teardown_app(AppSlot *a)
{ if (a != appslot_get(3) || resources != 1) bad++; teardown++; inject(2); }
static void exec_restore_context(int id)
{ if (id != 2 || owner != 2) bad++; g_cur_app = appslot_get(id); }
static void exec_notify_owned(int id, int kind)
{ if (id != 3 || kind != EXEC_KIND_EXITED || appslot_cur() != 2) bad++; notices++; }

#include "late_stop_appslot.inc"
#include "late_stop_exec.inc"

static int test(void)
{
    for (int phase = 0; phase <= 2; phase++) {
        for (int repeat = 0; repeat < 4; repeat++) {
            appslot_init();
            resources = teardown = notices = injections = bad = kills = 0;
            ring3_abort_count = appslot_reclaim_count = 0;
            inject_at = phase;
            appslot_start_commit(2, 0, 20);
            appslot_get(2)->cpl3 = 1;
            appslot_start_commit(3, 0, 30);
            appslot_get(3)->cpl3 = 1;
            g_cur_app = appslot_get(3);
            exec_finish(3, 17, EXEC_KIND_EXITED);
            CHECK(!bad && injections == (phase != 0), "request belongs to exiting child");
            CHECK(resources == 1 && teardown == 1 && notices == 1 &&
                  appslot_reclaim_count == 1, "cleanup once");
            CHECK(g_last_kind == EXEC_KIND_EXITED && g_last_code == 17 &&
                  exec_exit_status == 17, "normal result preserved");
            CHECK(appslot_cur() == 2 && owner == 2 && !appslot_get(2)->abort_req,
                  "parent request stays clear");
            ring3_abort_check(); ring3_abort_check(); /* parent's next safe points */
            CHECK(!kills && !ring3_abort_count, "parent not aborted");
            CHECK(!appslot_get(3) && !appslot_at(3)->abort_req,
                  "reclaimed slot request cleared");
            int next = appslot_start_admit(0, 30, 100);
            CHECK(next == 3, "reuse same slot");
            appslot_start_commit(next, 0, 30);
            appslot_get(next)->cpl3 = 1;
            g_cur_app = appslot_get(next);
            CHECK(!appslot_get(next)->abort_req, "reused slot request cleared");
            ring3_abort_check(); ring3_abort_check();
            CHECK(!kills && !ring3_abort_count, "reused slot not aborted");
            /* Positive control: a fresh STOP still reaches this new child,
             * is consumed once and leaves its parent alone. */
            ring3_abort_request();
            CHECK(appslot_get(next)->abort_req && !appslot_get(2)->abort_req,
                  "fresh STOP accepted");
            ring3_abort_check(); ring3_abort_check();
            CHECK(kills == 1 && ring3_abort_count == 1 && !appslot_get(next)->abort_req,
                  "fresh STOP consumed once");
        }
    }
    say("PASS late STOP: real exec_finish/AppSlot teardown request discarded; parent and reused slot survive; fresh STOP once\n");
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
