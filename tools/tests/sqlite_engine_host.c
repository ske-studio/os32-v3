#include "font_guard_inactive.h"
#define OS32_SQLITE_OUTER_IMPL
#include "types.h"
#include "sqlite_engine.h"
#include "sqlite_engine_body.h"
#include "os32_sqlite_outer.h"
#include "lib/sqlite3/generated/sqlite_facade.h"
static void die(int code) { __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); for (;;) {} }
static void say(const char *s) { u32 n=0; while(s[n]) n++; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n)); }
#define CHECK(x) do { if (!(x)) { say("FAIL: " #x "\n"); die(1); } } while (0)
static unsigned char stack[SQLITE_ENGINE_STACK_SIZE] __attribute__((aligned(16)));
u32 host_flags = 0x202;
volatile u32 kctx_irq_depth, kctx_exc_depth;
static int calls, kills, faults, pending;
static void *fault_env[5];
void host_stop(void) { faults++; __builtin_longjmp(fault_env, 1); }
typedef struct { int abort_req; } AppSlot;
static AppSlot app;
static AppSlot *g_cur_app = &app;
static int ring3_abort_count;
static int appslot_cur(void) { return 3; }
static AppSlot *appslot_get(int id) { (void)id; return &app; }
static void ring3_abort_kill(void) { kills++; }
#include "stop_source.c"
static const u32 test_eip = 0x12345678, test_cr2 = 0x87654321;
static u32 fault_values[3];
static int hex_count, screen_count, line_count;
static void sputs(const char *s) { if (s[0] == '\n' && !s[1]) line_count++; }
static void sput_hex32(u32 value) { CHECK(hex_count < 3); fault_values[hex_count++] = value; }
static void tvram_puts_at(int row, int col, const char *s, u8 attr)
{ (void)row; (void)col; (void)s; (void)attr; }
static void tvram_put_hex32(int row, int col, u32 value, u8 attr)
{ (void)row; (void)col; (void)attr; CHECK(value == fault_values[screen_count++]); }
#include "fault_source.c"
static void probe(void)
{
    char local;
    CHECK((u32)&local >= (u32)stack && (u32)&local < (u32)(stack + sizeof(stack)));
    CHECK(sqlite_engine_active == 1);
    calls++;
    if (pending) app.abort_req = 1;
}
static void nested(void *arg) { (void)arg; CHECK(0); }
static void callback(void *arg)
{
    probe();
    CHECK(arg == (void *)123);
    CHECK(!sqlite_engine_run(nested, 0, 0));
    CHECK(!sqlite_engine_available(0));
    CHECK(sqlite_engine_active == 1);
    #include "refused_calls.c"
}
#include "sqlite_mocks.c"
extern int check_registers(void (*fn)(void *), u32 top, void *arg);
u32 paging_sqlite_stack_guard;
extern u32 host_pool(void);
extern int host_init_called(void);
static unsigned char guarded[4096 + SQLITE_ENGINE_STACK_SIZE] __attribute__((aligned(4096)));
static int failure, allocations, shutdowns, present;
static SqliteModuleApi result_api;

u32 pgalloc_alloc_phys(u32 owner, int pages)
{
    CHECK(owner == 42);
    ++allocations;
    if (allocations == 1) {
        CHECK(pages == SQLITE_MODULE_POOL_SIZE / 4096);
        return failure == SQLITE_BODY_NO_POOL ? 0 : 4096;
    }
    CHECK(allocations == 2 && pages == sizeof(guarded) / 4096);
    return failure == SQLITE_BODY_NO_STACK ? 0 : (u32)guarded;
}

int paging_set_not_present(u32 start, u32 end)
{
    CHECK(start == (u32)guarded && end == start + 4095);
    present = failure == SQLITE_BODY_NO_GUARD;
    return 0;
}
int paging_is_present(u32 page) { CHECK(page == (u32)guarded); return present; }
static int remap_failure;
int paging_set_page(u32 va, u32 pa, u32 flags)
{
    CHECK(va == (u32)guarded && pa == va && flags == 2);
    CHECK(!host_init_called() && host_pool() == 4096);
    if (remap_failure) return 1;
    present = 1;
    return 0;
}
void *kmemset(void *dst, int value, u32 size)
{
    unsigned char *p = dst;
    while (size--) *p++ = value;
    return dst;
}
static int body_init(void *pool, SqliteModuleApi *out)
{
    char local;
    CHECK(pool == (void *)4096 && out == &result_api && host_init_called());
    CHECK(sqlite_engine_active && (u32)&local >= (u32)guarded + 4096 &&
          (u32)&local < sqlite_engine_stack_top && guarded[4096] == 0xa5);
    return failure == SQLITE_BODY_INIT ? SQLITE_ERROR : SQLITE_OK;
}
static int body_shutdown(void)
{
    CHECK(sqlite_engine_active && host_init_called() && !present);
    return ++shutdowns == 1 ? SQLITE_BUSY : SQLITE_OK;
}
static void test_body(void)
{
    for (failure = SQLITE_BODY_NO_POOL; failure <= SQLITE_BODY_INIT; failure++) {
        int rc;
        allocations = shutdowns = 0;
        CHECK(sqlite_engine_body_start(42, body_init, &result_api, &rc) == failure);
        CHECK(host_pool() == (failure == SQLITE_BODY_NO_POOL ? 0u : 4096u));
        CHECK(paging_sqlite_stack_guard == (failure >= SQLITE_BODY_NO_GUARD ? (u32)guarded : 0u));
        CHECK(!!sqlite_engine_stack_top == (failure == SQLITE_BODY_INIT));
        if (failure == SQLITE_BODY_INIT) {
            CHECK(rc == SQLITE_ERROR && host_init_called());
            sqlite_engine_active = 1;
            CHECK(sqlite_engine_body_stop(body_shutdown) == SQLITE_STOP_BUSY);
            sqlite_engine_active = 0;
            kctx_irq_depth = 1;
            CHECK(sqlite_engine_body_stop(body_shutdown) == SQLITE_STOP_BUSY);
            kctx_irq_depth = 0;
            CHECK(!shutdowns && host_init_called() && !present);
            CHECK(sqlite_engine_body_stop(body_shutdown) == SQLITE_STOP_SHUTDOWN);
            CHECK(shutdowns == 1 && host_init_called() && sqlite_engine_available(0));
            remap_failure = 1;
            CHECK(sqlite_engine_body_stop(body_shutdown) == SQLITE_STOP_REMAP);
            CHECK(!sqlite_engine_stack_top && !sqlite_engine_available(0) && !host_init_called() && !present);
            CHECK(host_pool() == 4096 && paging_sqlite_stack_guard == (u32)guarded);
            remap_failure = 0;
        }
        CHECK(sqlite_engine_body_stop(body_shutdown) == SQLITE_STOP_DONE);
        CHECK(!host_pool() && !paging_sqlite_stack_guard && !sqlite_engine_stack_top && !host_init_called());
        CHECK(sqlite_engine_body_stop(body_shutdown) == SQLITE_STOP_DONE);
        CHECK(shutdowns == (failure == SQLITE_BODY_INIT ? 2 : 0));
    }
}
void _start(void)
{
    test_body();
    CHECK(!sqlite_engine_available(1));
    #include "stub_calls.c"
    CHECK(!calls);
    sqlite_engine_stack_top = (u32)(stack + sizeof(stack));
    sqlite_module_api = &exports;
    CHECK(sqlite_engine_available(1));
    pending = 1;
    CHECK(sqlite_engine_run(callback, (void *)123, 1));
    CHECK(!sqlite_engine_active && !kills && host_flags == 0x202);
    ring3_abort_check(); ring3_abort_check();
    CHECK(kills == 1 && ring3_abort_count == 1 && !app.abort_req);
    pending = 0;
    int before = calls;
    kctx_irq_depth = 1;
    #include "refused_calls.c"
    CHECK(!sqlite_engine_run(callback, (void *)123, 0));
    kctx_irq_depth = 0; kctx_exc_depth = 1;
    #include "refused_calls.c"
    CHECK(!sqlite_engine_run(callback, (void *)123, 0));
    kctx_exc_depth = 0; host_flags = 2;
    #include "io_refused_calls.c"
    CHECK(!sqlite_engine_run(callback, (void *)123, 1));
    CHECK(calls == before && host_flags == 2);
    CHECK(sqlite_engine_run(callback, (void *)123, 0));
    CHECK(host_flags == 2);
    host_flags = 0x202;
    /* Real assembly, all four callee-saved registers and exact ESP. */
    sqlite_engine_active = 1;
    CHECK(check_registers(callback, sqlite_engine_stack_top, (void *)123));
    sqlite_engine_active = 0;
    #include "outer_calls.c"
    CHECK(!sqlite_engine_active && host_flags == 0x202);
    CHECK(os32_sqlite_get_autocommit((sqlite3 *)7) == 73);
    CHECK(os32_sqlite_outer_db_fd((void *)7) == 73);
    CHECK(!sqlite_engine_active);
    sqlite_module_api = &sqlite_stub_api;
    #include "stub_calls.c"
    /* The actual handler gates must stop before the app-kill branch. */
    for (int kind = 0; kind < 2; kind++) {
        sqlite_engine_active = 1;
        hex_count = screen_count = line_count = 0;
        if (!__builtin_setjmp(fault_env)) {
            if (kind) test_page_fault(); else test_exception();
            CHECK(0);
        }
        CHECK(sqlite_engine_active == 1 && kills == 1);
        CHECK(hex_count == 3 && screen_count == 3 && line_count == 1);
        CHECK(fault_values[0] == (kind ? 14u : 13u));
        CHECK(fault_values[1] == test_eip);
        CHECK(fault_values[2] == (kind ? test_cr2 : 0));
    }
    CHECK(faults == 2);
    sqlite_engine_active = 0; host_flags = 0x202;
    say("PASS: all typed outer calls/engine-stack ESP/registers/IF/reentry/IRQ/STOP/fault\n");
    die(0);
}
