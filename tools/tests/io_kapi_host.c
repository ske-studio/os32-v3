#include "io_kapi.h"
#include "io_policy.h"
#include "io_work.h"
#include "io.h"
#include "appslot.h"
#include "shell_service.h"
#include "os32_kapi_shared.h"
#define CHECK(x) do { if (!(x)) return 1; } while (0)
static AppSlot slots[6];
static struct addrspace spaces[6];
ShellService g_shell_service;
volatile u32 kctx_irq_depth, kctx_exc_depth;
static unsigned int flags = 1, pump_budget, pump_calls;
static u32 total_count;
u32 io_dispatch_total_count(void) { return total_count; }
static int owner = 2, busy, clock_rc, reclaim_order[32], reclaim_count;
AppSlot *appslot_get(int id) { return id > 0 && id < 6 && slots[id].state ? &slots[id] : 0; }
int res_owner_get(void) { return owner; }
void res_owner_set(int id) { owner = id; }
int _irq_enabled(void) { return flags != 0; }
unsigned int irq_save(void) { unsigned int f = flags; flags = 0; return f; }
void irq_restore(unsigned int f) { flags = f; }
int io_dispatch_running(void) { return busy; }
int io_clock_us(u32 *lo, u32 *hi) { *lo = *hi = 0; return clock_rc; }
int io_dispatch(u32 budget) { pump_budget = budget; ++pump_calls; return 3; }
static void start(int id, u32 gen, int parent)
{
    slots[id].state = 1; slots[id].gui = 0; slots[id].parent = parent;
    slots[id].cpl3 = id != 1; spaces[id].generation = gen; slots[id].as = &spaces[id];
}
static void le32(u8 *p, u32 x) { for (unsigned int i = 0; i < 4; ++i) p[i] = (u8)(x >> (8 * i)); }
static int set_budget(u32 budget)
{
    u8 p[32] = {'I','O','T','1',1,0,16,0,1};
    le32(p + 16, 1); p[20] = 2; le32(p + 24, budget);
    return kapi_io_policy_set(p, sizeof(p));
}
static u32 budget(void) { u32 b = 0; io_policy_lookup(1, 0, &b); return b; }
/* Execute the real finish/kill/common-resource bodies; unrelated resources
 * and address-space destruction are fixture dependencies. */
#define NOOP_ID(name) static void name(int id) { (void)id; }
NOOP_ID(exec_input_owner_exit)
NOOP_ID(kapi_strings_cleanup_owned)
NOOP_ID(exec_db_cleanup_owned)
NOOP_ID(fd_redirect_reset_owned)
NOOP_ID(exec_font_lease_close_owned)
NOOP_ID(vfs_close_owned)
NOOP_ID(install_reclaim_owner)
NOOP_ID(pipe_free_owned)
NOOP_ID(shm_free_owned)
NOOP_ID(exec_pcm_reclaim)
NOOP_ID(in2_reclaim)
static AppSlot *g_cur_app;
static int exec_exit_status, g_last_kind, g_last_code, exec_nest_level;
static u32 ring3_transition_count;
static int g_exit_jmpbuf;
static int appslot_cur(void) { return owner; }
static int appslot_return_target(int id) { return slots[id].parent; }
static int paging_kernel_pd_phys(void) { return 1; }
NOOP_ID(paging_load_cr3)
NOOP_ID(exec_restore_context)
NOOP_ID(appslot_switch_to)
static void k_jmp_copy(int dst, int src) { (void)dst; (void)src; }
static void exec_teardown_app(AppSlot *a) { a->as->generation = 0; }
static void appslot_reclaim(int id) { slots[id].state = 0; reclaim_order[reclaim_count++] = id; }
static void exec_notify_owned(int id, int kind) { (void)id; (void)kind; }
static void shell_service_retire(void) { }
static int exec_font_guard_active(void) { return 0; }
static int appslot_kill_check(int id) { return appslot_get(id) && id != owner ? 0 : OS32_ERR_INVAL; }
static int launch_chain(int id, int *chain, int cap)
{
    int n = 0;
    while (id && n < cap) {
        chain[n++] = id;
        int next = 0;
        for (int i = 2; i < 6; ++i) if (slots[i].state && slots[i].parent == id) next = i;
        id = next;
    }
    return n;
}
#include "reclaim.inc"
#include "park.inc"
#include "child.inc"
static int exercise(void)
{
    unsigned int needed = 0;
    u8 bytes[80];
    start(1, 0, 0); g_shell_service.generation = 91;
    start(2, 17, 1); start(3, 29, 2);
    CHECK(kapi_io_policy_set(0, 0) == OS32_ERR_INVAL);
    CHECK(kapi_io_policy_get(bytes, sizeof(bytes), 0) == OS32_ERR_INVAL);
    for (unsigned int i = 0; i < sizeof(bytes); ++i) bytes[i] = 0xa5;
    CHECK(kapi_io_policy_get(bytes, 79, &needed) == OS32_ERR_FULL && needed == 80);
    for (unsigned int i = 0; i < sizeof(bytes); ++i) CHECK(bytes[i] == 0xa5);
    CHECK(kapi_io_policy_get(bytes, 80, &needed) == 0 && needed == 80);
    busy = 1;
    CHECK(set_budget(40) == OS32_ERR_BUSY);
    CHECK(kapi_io_policy_reset() == OS32_ERR_BUSY);
    busy = 0;
    CHECK(kapi_io_pump(0) == OS32_ERR_INVAL && pump_calls == 0);
    clock_rc = OS32_ERR_NOSYS;
    CHECK(kapi_io_pump(500) == OS32_ERR_NOSYS && pump_calls == 0);
    clock_rc = 0;
    CHECK(kapi_io_pump(999) == 3 && pump_budget == IO_PASS_US && pump_calls == 1);
    CHECK(kapi_io_pump(23) == 3 && pump_budget == 23 && pump_calls == 2);
    for (int ctx = 0; ctx < 3; ++ctx) {
        flags = ctx != 0; kctx_irq_depth = ctx == 1; kctx_exc_depth = ctx == 2;
        CHECK(set_budget(60) == OS32_ERR_BUSY);
        CHECK(kapi_io_policy_get(bytes, 80, &needed) == OS32_ERR_BUSY);
        CHECK(kapi_io_policy_reset() == OS32_ERR_BUSY);
        CHECK(kapi_io_pump(20) == OS32_ERR_BUSY && pump_calls == 2);
    }
    flags = 1; kctx_irq_depth = kctx_exc_depth = 0;
    /* Normal and fault use the same actual exec_finish; owner has already
     * changed on kill, so reclaim must use the supplied ID and saved gen. */
    for (int kind = 0; kind < 3; ++kind) {
        start(2, 100 + kind, 1); owner = 2;
        io_syscall_max_count = 0; total_count = 0;
        if (kind < 2) io_syscall_begin(2);
        if (kind == 1) io_syscall_begin(2); /* fault abandons both entries */
        total_count = 17 + kind;
        CHECK(set_budget(42) == 0 && budget() == 42);
        io_policy_owner_exit(2, 99 + kind);
        CHECK(budget() == 42);
        if (kind < 2) exec_finish(2, -3, kind ? EXEC_KIND_FAULT : EXEC_KIND_EXITED);
        else { owner = 1; CHECK(exec_kill(2) == 0); }
        CHECK(budget() == 120);
        if (kind < 2) CHECK(io_syscall_max_count == (u32)(17 + kind));
    }
    /* Parent/child restore in reverse order, including reset's saved source. */
    start(2, 201, 1); start(3, 202, 2); owner = 2;
    CHECK(set_budget(51) == 0);
    owner = 3; CHECK(set_budget(73) == 0);
    CHECK(kapi_io_policy_reset() == 0 && budget() == 120);
    exec_finish(3, 0, EXEC_KIND_EXITED); CHECK(budget() == 51);
    exec_finish(2, 0, EXEC_KIND_EXITED); CHECK(budget() == 120);
    start(2, 203, 1); start(3, 204, 2); owner = 2; CHECK(set_budget(52) == 0);
    owner = 3; CHECK(set_budget(74) == 0); owner = 1; reclaim_count = 0;
    CHECK(exec_kill(2) == 0 && budget() == 120);
    CHECK(reclaim_count == 2 && reclaim_order[0] == 3 && reclaim_order[1] == 2);
    start(2, 205, 1); owner = 1; CHECK(set_budget(85) == 0);
    owner = 2; CHECK(kapi_io_policy_reset() == 0 && budget() == 120);
    exec_finish(2, 0, EXEC_KIND_EXITED); CHECK(budget() == 85);
    exec_finish(1, 0, EXEC_KIND_EXITED); CHECK(budget() == 120);
    owner = 1; CHECK(set_budget(88) == 0); exec_finish(1, 0, EXEC_KIND_EXITED); CHECK(budget() == 120);
    owner = 0; CHECK(kapi_io_policy_reset() == OS32_ERR_INVAL);
    start(2, 0, 1); owner = 2; CHECK(set_budget(42) == OS32_ERR_INVAL);
    io_syscall_max_count = 0; total_count = 0; owner = 2; fixture_enter();
    total_count = 13; io_syscall_end(2); CHECK(io_syscall_max_count == 13);
    /* N5/P2: wrap, inactive end, and a longer subsequent interval. */
    io_syscall_max_count = 0; total_count = 0xfffffff0u; io_syscall_begin(2);
    total_count = 32; io_syscall_end(2); CHECK(io_syscall_max_count == 48);
    total_count = 80; io_syscall_end(2); CHECK(io_syscall_max_count == 48);
    total_count = 100; io_syscall_begin(2);
    total_count = 190; io_syscall_end(2); CHECK(io_syscall_max_count == 90);
    io_syscall_max_count = 0; total_count = 0; io_syscall_begin(2);
    io_syscall_begin(2); /* park also abandons both entries */
    total_count = 29; fixture_park(2); CHECK(io_syscall_max_count == 29);
    /* Reentrant WM callbacks share the outer owner's accounting interval. */
    io_syscall_max_count = 0; total_count = 200; io_syscall_begin(2);
    total_count = 250; io_syscall_begin(2);
    total_count = 270; io_syscall_end(2); CHECK(io_syscall_max_count == 0);
    total_count = 300; io_syscall_end(2); CHECK(io_syscall_max_count == 100);
    /* exec_run's child may issue many syscalls; none belong to the parent. */
    for (unsigned int nested = 0; nested < 2; ++nested) {
        io_syscall_max_count = 0; total_count = 0; io_syscall_begin(2);
        if (nested) io_syscall_begin(2);
        total_count = 10;
        u32 depth = fixture_child_commit(2);
        CHECK(io_syscall_max_count == 10);
        for (unsigned int i = 0; i < 6; ++i) {
            io_syscall_begin(3); total_count += 10; io_syscall_end(3);
        }
        CHECK(io_syscall_max_count == 10);
        fixture_parent_return(2, depth);
        total_count += 15;
        if (nested) { io_syscall_end(2); CHECK(io_syscall_max_count == 10); }
        total_count += 5; io_syscall_end(2);
        CHECK(io_syscall_max_count == 20);
    }
    /* A trusted launcher with no active syscall must remain inactive. */
    u32 depth = fixture_child_commit(1);
    CHECK(depth == 0); fixture_parent_return(1, depth);
    total_count += 100; io_syscall_end(1); CHECK(io_syscall_max_count == 20);
    return 0;
}
void _start(void)
{
    int rc = exercise();
#if defined(__PPC__)
    register int r0 __asm__("r0") = 1;
    register int r3 __asm__("r3") = rc;
    __asm__ volatile("sc" : : "r"(r0), "r"(r3) : "memory");
#else
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
#endif
    __builtin_unreachable();
}
