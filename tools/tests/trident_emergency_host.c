/* IF=0 dispatcher and actual exception/exit prefixes, libc-free ILP32.
 * No device sequence oracle here: tri-life owns the real regs assembly test.
 * The shared device fake starts cold; the recovery callback models vtSema. */
#include "types.h"
#include "os32_kapi_shared.h"
#include "gfx/gfx_emergency.h"
#include "trident_device_fake.h"
#include "idt.h"
#include "appslot.h"

static struct trident_fake fake;
static struct trident_device device;
static u8 vram[TRI_FAKE_REAL_BYTES];
static int owner = 2, calls, returned, rc, recurse, killed, tvram, transferred;
static void *landing[5];
static int failures;
static void emit(const char *s)
{
    u32 n = 0; while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(x) do { int ok_ = !!(x); __asm__ volatile("" : "+r"(ok_) : : "memory"); \
    if (!ok_) { emit("RED " #x "\n"); failures++; } } while (0)
int appslot_gfx_owner(void) { return owner; }
static int restore(void *opaque)
{
    CHECK(opaque == &device);
    calls++;
    /* Guard the fixture too, so the no-recursion mutant fails without a
     * stack overflow or timeout. Product guard must stop the nested call. */
    if (recurse && calls < 3) {
        CHECK(!gfx_return_emergency(GFX_EMERGENCY_KERNEL_OWNER, 101));
        CHECK(gfx_arm_emergency(restore, opaque, 2) == OS32_ERR_BUSY);
    }
    if (device.active) { device.active = false; returned++; }
    return rc;
}
static int other_restore(void *p) { (void)p; return 0; }

static int font_guard_active, sqlite_engine_active, ring3_in_syscall, ring3_wm_depth;
static void _disable(void) { }
static void font_guard_fault(u32 v, u32 e, u32 a) { (void)v; (void)e; (void)a; }
static void sqlite_engine_fault(u32 v, u32 e, u32 a) { (void)v; (void)e; (void)a; }
static void sputs(const char *s) { (void)s; }
static void sput_hex32(u32 n) { (void)n; }
static void ring3_fault_kill(void) { killed++; __builtin_longjmp(landing, 1); }
#define EXC_SCREEN_CLEAR_ROWS 15
#define EXC_ATTR_LEGACY 7
static void tvram_puts_at(int r, int c, const char *s, u8 a)
{
    (void)r; (void)c; (void)s; (void)a;
    CHECK(calls && !device.active); tvram++;
}
static void input_discard_stop(void) { }
static void exec_ime_owner_exit(int id) { (void)id; }
static void gui_owner_exit(int id, int kind) { (void)id; (void)kind; }
static void kbd_inject_owner_exit(int id) { (void)id; }
static int con_sink_is_enabled(void) { return 0; }
static int con_sink_reader_get(void) { return 0; }
static void con_sink_push_exit(int id) { (void)id; }
static int launch_child(int id) { (void)id; return 0; }
static void kbd_inject_discard(void) { }
static void launch_owner_exit(int id) { (void)id; }
static void exec_host_owner_exit(int id) { (void)id; }
static void con_sink_owner_exit(int id) { (void)id; }
void appslot_gfx_owner_exit(int id)
{
    if (id == owner) { CHECK(!device.active); owner = 1; transferred++; }
}
static void kprintf(u8 a, const char *s, ...) { (void)a; (void)s; }
static int exec_gfx_return_emergency(u32 owner_id, int reason) { return gfx_return_emergency(owner_id, reason); }
#include "trident_emergency_entries.inc"

static void faults(int pf, int user, int syscall, int error)
{
    u32 regs[16] = {0};
    regs[pf ? ISR_PF_CS : ISR_COMMON_CS] = user ? 3 : 0;
    owner = 2; device.active = true; calls = tvram = killed = 0;
    ring3_in_syscall = syscall; rc = error;
    if (!__builtin_setjmp(landing)) {
        if (pf) page_fault_handler(1, 0, 0, regs);
        else exception_handler(0, 13, 0, regs);
    }
    if (user || syscall) CHECK(killed == 1 && !calls && !tvram && device.active);
    else CHECK(!killed && calls == 1 && tvram && !device.active);
}
void _start(void)
{
    CHECK(!trident_fake_reset(&fake, vram, sizeof(vram), 0, 0));
    trident_fake_bind(&fake, &device);
    CHECK(!device.active && !device.armed && !fake.pc98_initialized);
    CHECK(!gfx_return_emergency(0, -99));
    CHECK(gfx_arm_emergency(0, &device, 2) == OS32_ERR_INVAL);
    CHECK(!gfx_arm_emergency(restore, &device, 2));
    CHECK(!gfx_arm_emergency(restore, &device, 3));
    CHECK(gfx_arm_emergency(other_restore, &device, 2) == OS32_ERR_BUSY);
    CHECK(gfx_arm_emergency(restore, &fake, 2) == OS32_ERR_BUSY);
    device.active = true;
    CHECK(!gfx_return_emergency(3, -17) && !calls && device.active);
    owner = 3; rc = -71;
    CHECK(!gfx_return_emergency(2, -17) && !calls);
    CHECK(gfx_return_emergency(3, -17) == -71 && calls == 1 && returned == 1);
    rc = 0; CHECK(!gfx_return_emergency(3, -17) && returned == 1);
    device.active = true; calls = 0; recurse = 1;
    CHECK(!gfx_return_emergency(0, -17) && calls == 1 && returned == 2);
    recurse = 0;
    for (int pf = 0; pf < 2; pf++)
        for (int user = 0; user < 2; user++)
            for (int syscall = 0; syscall < 2; syscall++)
                for (int fail = 0; fail < 2; fail++)
                    faults(pf, user, syscall, fail ? -72 : 0);
    for (int kind = EXEC_KIND_EXITED; kind <= EXEC_KIND_ABORTED; kind++) {
        owner = 2; device.active = true; calls = transferred = 0; rc = -73;
        exec_notify_owned(3, kind);
        CHECK(!calls && device.active && !transferred);
        exec_notify_owned(2, kind);
        CHECK(calls == 1 && !device.active && transferred == 1 && owner == 1);
    }
    if (!failures) emit("GREEN emergency: live owner, IF=0, recursion, both faults, exit/fault/STOP\n");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(!!failures) : "memory");
    __builtin_unreachable();
}
