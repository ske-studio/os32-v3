#define OS32_SQLITE_OUTER_IMPL
#include "types.h"
#include "sqlite_engine.h"
#include "os32_sqlite_outer.h"
static void die(int code) { __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); for (;;) {} }
static void say(const char *s) { u32 n=0; while(s[n]) n++; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n)); }
#define CHECK(x) do { if (!(x)) { say("FAIL: " #x "\n"); die(1); } } while (0)
static unsigned char stack[128 * 1024] __attribute__((aligned(16)));
u32 host_flags = 0x202;
volatile u32 kctx_irq_depth, kctx_exc_depth;
static int calls, kills, faults, pending;
static int expected_owner = 3;
static void *fault_env[5];
int res_owner_get(void) { return 3; }
void host_stop(void) { faults++; __builtin_longjmp(fault_env, 1); }
typedef struct { int abort_req; } AppSlot;
static AppSlot app;
static AppSlot *g_cur_app = &app;
static int ring3_abort_count;
static int appslot_cur(void) { return 3; }
static AppSlot *appslot_get(int id) { (void)id; return &app; }
static void ring3_abort_kill(void) { kills++; }
#include "stop_source.c"
#include "fault_source.c"
static void probe(void)
{
    char local;
    CHECK((u32)&local >= (u32)stack && (u32)&local < (u32)(stack + sizeof(stack)));
    CHECK(sqlite_engine_active == 1 && sqlite_engine_owner == expected_owner);
    if (expected_owner == 1) CHECK(sqlite_engine_group == 2 && sqlite_engine_generation == 9);
    calls++;
    if (pending) { app.abort_req = 1; ring3_abort_check(); CHECK(!kills && app.abort_req); }
}
static void nested(void *arg) { (void)arg; CHECK(0); }
static void callback(void *arg)
{
    probe();
    CHECK(arg == (void *)123);
    CHECK(!sqlite_engine_run(nested, 0, 0));
    CHECK(!sqlite_engine_available(0));
    CHECK(sqlite_engine_active == 1);
}
#include "sqlite_mocks.c"
extern int check_registers(void (*fn)(void *), u32 top, void *arg);
void _start(void)
{
    sqlite_engine_stack_top = (u32)(stack + sizeof(stack));
    CHECK(sqlite_engine_available(1));
    pending = 1;
    CHECK(sqlite_engine_run(callback, (void *)123, 1));
    CHECK(!sqlite_engine_active && !kills && host_flags == 0x202);
    ring3_abort_check(); ring3_abort_check();
    CHECK(kills == 1 && ring3_abort_count == 1 && !app.abort_req);
    pending = 0;
    int before = calls;
    kctx_irq_depth = 1;
    CHECK(!sqlite_engine_run(callback, (void *)123, 0));
    kctx_irq_depth = 0; kctx_exc_depth = 1;
    CHECK(!sqlite_engine_run(callback, (void *)123, 0));
    kctx_exc_depth = 0; host_flags = 2;
    CHECK(!sqlite_engine_run(callback, (void *)123, 1));
    CHECK(calls == before && host_flags == 2);
    CHECK(sqlite_engine_run(callback, (void *)123, 0));
    CHECK(host_flags == 2);
    host_flags = 0x202;
    /* Real assembly, all four callee-saved registers and exact ESP. */
    sqlite_engine_active = 1; sqlite_engine_owner = 3;
    CHECK(check_registers(callback, sqlite_engine_stack_top, (void *)123));
    sqlite_engine_active = 0;
    #include "outer_calls.c"
    CHECK(!sqlite_engine_active && host_flags == 0x202);
    expected_owner = 1;
    CHECK(os32_sqlite_get_autocommit((sqlite3 *)7) == 73);
    CHECK(os32_sqlite_outer_db_fd((void *)7) == 12);
    CHECK(!sqlite_engine_active && sqlite_engine_group == -1 && !sqlite_engine_generation);
    expected_owner = 3;
    /* The actual handler gates must stop before the app-kill branch. */
    for (int kind = 0; kind < 2; kind++) {
        sqlite_engine_active = 1;
        if (!__builtin_setjmp(fault_env)) {
            if (kind) test_page_fault(); else test_exception();
            CHECK(0);
        }
        CHECK(sqlite_engine_active == 1 && kills == 1);
    }
    CHECK(faults == 2);
    sqlite_engine_active = 0; host_flags = 0x202;
    say("PASS: all typed outer calls/128KiB ESP/registers/IF/reentry/IRQ/STOP/fault\n");
    die(0);
}
