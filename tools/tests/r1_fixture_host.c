/* Real ledger and stop paths; only CPU entry and stop are host substitutes. */
#include "types.h"
#include "v86_mem.h"
#include "r1_fixture.h"
static void *stop_jmp[5];
static unsigned int host_if = 0x202;
static void _stop(void) { __builtin_longjmp(stop_jmp, 1); }
static void _disable(void) { host_if &= ~0x200U; }
static unsigned int irq_save(void) { unsigned int f = host_if; _disable(); return f; }
static void irq_restore(unsigned int f) { host_if = f; }
#include "pgalloc_source.c"
#include "pgalloc_host_fixture.h"
int paging_boot_context(void) { return 1; }
void *paging_phys_identity_ptr(u32 phys, u32 bytes) { (void)bytes; return (void *)phys; }
struct v86_session_state v86_session;
volatile u32 exec_stop_count;
static const char *serial_line;
static void serial_puts_polled(const char *s) { serial_line = s; }
static int equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void fail(int line) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(line % 200 + 1));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
/* Unreached non-closing continuation is deliberately fatal in this fixture. */
typedef struct { int state; u32 jmpbuf[16]; } AppSlot;
static AppSlot app;
static int appslot_cur(void) { return 2; }
static AppSlot *appslot_get(int id) { return id == 2 ? &app : 0; }
static int g_pending_id, g_pending_kind, g_longjmp_reason;
#define EXEC_KIND_FAULT 3
#define EXEC_KIND_ABORTED 2
#define APP_STATE_ABORT_PENDING 4
#define APP_STATE_FAULT_PENDING 5
#define EXEC_LJ_PENDING 3
static void ring3_context_clear(void) { fail(__LINE__); }
void exec_longjmp(u32 *buf) { (void)buf; fail(__LINE__); }
#include "exec_stop_source.c"
static void ring3_fault_kill(void) { exec_pending_transfer(EXEC_KIND_FAULT); }
volatile int ring3_in_syscall = 1;
static int ring3_wm_depth;
static void sputs(const char *s) { (void)s; }
static void sput_hex32(u32 v) { (void)v; }
#include "entry_source.c"
static void __attribute__((unused)) host_raise_ud(void) {
    u32 regs[13] = {0};
    kctx_exc_depth++;
    exception_handler(0, R1_FIXTURE_UD_VECTOR, 0, regs);
    fail(__LINE__);
}
#include "fixture_source.c"
static void snd_tick(void) {}
static void pcm_tick(void) {}
static void ne2k_timer_tick(void) {}
static void link_tick(void) {}
static void lgy98_tick(void) {}
#include "timer_source.c"

static int test(void)
{
    host_pool_boot(16 * 1024);
    u32 free0 = pgalloc_free_pages();
    /* Unarmed and invalid magic: no side effects, even in forbidden contexts. */
    for (u32 arm = 0; arm < 2; arm++) {
        r1_fixture_arm[1] = r1_fixture_arm[2] = r1_fixture_arm[4] = arm;
        kctx_irq_depth = 1; timer_handler(); kctx_irq_depth = 0;
        kctx_exc_depth = 1; r1_fixture_exception(R1_FIXTURE_UD_VECTOR); kctx_exc_depth = 0;
        v86_session.open = 1; v86_session.closing = 1; r1_fixture_v86_end();
        CHECK(!ledger_irq_ops && !ledger_exc_ops && !exec_stop_count);
        CHECK(pgalloc_free_pages() == free0);
    }
    r1_fixture_arm[1] = R1_FIXTURE_ARM;
    kctx_irq_depth = 1;
    if (!__builtin_setjmp(stop_jmp)) { timer_handler(); fail(__LINE__); }
    CHECK(!r1_fixture_arm[1] && ledger_irq_ops == 1 && !ledger_exc_ops);
    CHECK(equal(ledger_check_tag, "R1 context"));
    CHECK(ledger_irq_last[0] == LEDGER_OP_ALLOC && ledger_irq_last[1] == LEDGER_OWNER_KERNEL && ledger_irq_last[2]);
    CHECK(ledger_irq_last[2] >= (u32)r1_fixture_timer &&
          ledger_irq_last[2] < (u32)r1_fixture_exception);
    CHECK(pgalloc_free_pages() == free0);
    timer_handler(); CHECK(ledger_irq_ops == 1); kctx_irq_depth = 0;

    r1_fixture_arm[2] = R1_FIXTURE_ARM;
    kctx_exc_depth = 1;
    r1_fixture_exception(5); CHECK(r1_fixture_arm[2] == R1_FIXTURE_ARM);
    if (!__builtin_setjmp(stop_jmp)) {
        u32 regs[13] = {0};
        exception_handler(0, R1_FIXTURE_UD_VECTOR, 0, regs); fail(__LINE__);
    }
    CHECK(!r1_fixture_arm[2] && ledger_exc_ops == 1);
    CHECK(equal(ledger_check_tag, "R1 context"));
    CHECK(ledger_exc_last[0] == LEDGER_OP_FREE && ledger_exc_last[1] == LEDGER_OWNER_KERNEL && ledger_exc_last[2]);
    CHECK(ledger_exc_last[2] >= (u32)r1_fixture_exception &&
          ledger_exc_last[2] < (u32)r1_fixture_v86_end);
    CHECK(pgalloc_free_pages() == free0);
    r1_fixture_exception(R1_FIXTURE_UD_VECTOR); CHECK(ledger_exc_ops == 1); kctx_exc_depth = 0;

    r1_fixture_arm[4] = R1_FIXTURE_ARM;
    v86_session.open = 0; r1_fixture_v86_end(); CHECK(r1_fixture_arm[4] == R1_FIXTURE_ARM);
    v86_session.open = 1; v86_session.closing = 0;
    ring3_in_syscall = 0; r1_fixture_v86_end();
    CHECK(r1_fixture_arm[4] == R1_FIXTURE_ARM); ring3_in_syscall = 1;
    if (!__builtin_setjmp(stop_jmp)) { v86_session_end(); fail(__LINE__); }
    CHECK(!r1_fixture_arm[4] && v86_session.closing && exec_stop_count == 1);
    CHECK(equal(serial_line, "OS32: exec teardown stopped\r\n"));
    CHECK(ledger_irq_ops == 1 && ledger_exc_ops == 1 && pgalloc_free_pages() == free0);
    r1_fixture_v86_end(); CHECK(exec_stop_count == 1);
    return 0;
}
void _start(void) {
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
