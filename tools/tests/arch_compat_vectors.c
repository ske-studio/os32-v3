/* Shared host vectors: provider selection and privileged operations are seams. */
#include "types.h"
#ifdef INPUT_SOURCE
/* PC98 table wiring uses a recording keyboard; PowerMac runs its real reader.
 * These seams do not establish device, IRQ, or scheduler conformance. */
#include "input_service.h"
#include "kernel/input_service.c"
static int queued = -1, polls, delay, cancelled, waited, raw_key = -1, down;
static u32 mods;
static int take(void)
{
    ++polls;
    if (delay && --delay) return -1;
    int c = queued; queued = -1; return c;
}
static void feed(int c, int after)
{ queued = c; polls = 0; delay = after; }
#ifdef PC98_INPUT
/* Only the device-facing functions are fake; the InputOps initializer is real. */
volatile u8 kbd_shift_state;
int kbd_trygetkey(void) { return take(); }
int kbd_trygetchar(void) { int c = take(); return c < 0 ? c : c & 255; }
int kbd_trygetchar_local(void) { return kbd_trygetchar(); }
int kbd_peekkey(void) { return queued; }
int kbd_has_key(void) { return queued >= 0; }
int kbd_getkey_wait(int interruptible)
{
    ++waited;
    if (interruptible && cancelled) return -1;
    int c; while ((c = take()) < 0) {} return c;
}
int kbd_getkey(void) { return kbd_getkey_wait(0); }
int kbd_getchar(void) { return kbd_getkey() & 255; }
int kbd_getchar_kapi(void) { return kbd_getkey_wait(1); }
int kbd_getkey_kapi(void) { return kbd_getkey_wait(1); }
int kbd_is_pressed(int scancode) { return scancode == KEY_CTRL && down; }
int kbd_trygetrawkey(void) { int c = raw_key; raw_key = -1; return c; }
u32 kbd_get_modifiers(void) { return mods; }
u32 kbd_dropped_count(void) { return 0; }
void kbd_set_gui_mode(int on) { (void)on; }
void kbd_discard_stop(void) {}
int kbd_diag(KbdDiag *out) { (void)out; return OS32_ERR_NOSYS; }
int kbd_diag_log(u32 seq, KbdDiagLogEnt *out, int max)
{ (void)seq; (void)out; (void)max; return OS32_ERR_NOSYS; }
#else
static u32 fake_time;
static int capture;
int con_sink_capture_active(void) { return capture; }
int ring3_wait_pending(void) { return cancelled; }
void idle_work_run(void);
void _idle(void);
void idle_wait_step(void) { idle_work_run(); _idle(); }
void idle_work_run(void) {}
void _idle(void) { fake_time += 1000000; }
int pm_input_usb_take(void) { return take(); }
int pm_input_usb_peek(void) { return queued; }
u32 pm_input_usb_dropped(void) { return 0; }
int pm_terminal_trygetkey(void) { return -1; }
void pm_terminal_poll(void) {}
int platform_clock_read_us(u32 *lo, u32 *hi) { *lo = fake_time; *hi = 0; return 0; }
#include "kernel/input_legacy.c"
#include "kernel/input_wait.c"
#endif
#include INPUT_SOURCE
static int hits, shutdowns;
static int pointer_present(void) { return 1; }
#define KAPI_HIT(slot) (hits = (slot))
const char *path_get_drive_user(void) { return "A:"; }
int ime_is_active(void) { return 1; }
void gfx_shutdown(void) { ++shutdowns; }
#include KAPI_SOURCE
#define REQUIRE(expr) do { if (!(expr)) return 64; } while (0)
static int input_kapi(void)
{
    PointerOps pointer = empty_pointer;
    pointer.available = pointer_present;
    pointer_register(&pointer);
#ifdef PC98_INPUT
    input_register(&pc98_input);
#else
    pm_input_boot();
#endif
    /* All waiting calls below are foreground calls with a finite producer.
     * An IRQ caller is deliberately not granted a new waiting contract. */
    static const int keys[] = {-1, 0, 'A', 0x3a00, 0x1e61};
    int failures = 0;
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        int c = keys[i];
        feed(c, 0);
        REQUIRE(input_peekkey() == c && input_peekkey() == c);
        REQUIRE(input_has_key() == (c >= 0));
        REQUIRE(wrap_kbd_trygetkey() == c && hits == TRY_SLOT);
        REQUIRE(input_trygetkey() == -1);
        feed(c, 0); REQUIRE(input_trygetchar() == (c < 0 ? -1 : c & 255));
        feed(c, 0); REQUIRE(input_trygetchar_local() == (c < 0 ? -1 : c & 255));
    }
    REQUIRE(waited == 0); /* try must never delegate to a blocking operation. */
    feed('W', 3); REQUIRE(input_getkey_wait(0) == 'W' && polls == 3);
#ifndef PC98_INPUT
    feed('A', 0); REQUIRE(input_peekkey() == 'A');
    /* USB peek leaves the product queue intact, without a private copy. */
    REQUIRE(queued == 'A' && input_getkey_wait(0) == 'A');
    feed('B', 0); REQUIRE(input_getkey_wait(0) == 'B');
    capture = 1; fake_time = 17000000; feed(-1, 0);
    REQUIRE(input_getchar() == 13 && fake_time == 47000000 && capture);
    REQUIRE(input_getkey_wait(0) == 27 && fake_time == 77000000 && capture);
    capture = 0;
#endif
    cancelled = 1; feed('W', 3);
    int result = input_getkey_wait(1);
    if (result != -1) { REQUIRE(result == 'W' && polls == 3); failures |= 1; }
    cancelled = 0;
    const int make_ctrl = KEY_CTRL | KBD_RAW_MAKE | (SHIFT_CTRL << KBD_RAW_MOD_SHIFT);
    raw_key = make_ctrl; down = 1; mods = SHIFT_CTRL;
    result = input_trygetrawkey();
    if (result != make_ctrl) { REQUIRE(result == -1); failures |= 2; }
    result = input_is_pressed(KEY_CTRL);
    if (result != 1) { REQUIRE(result == 0); failures |= 4; }
    u32 modifiers = input_get_modifiers();
    if (modifiers != SHIFT_CTRL) { REQUIRE(modifiers == 0); failures |= 8; }
    raw_key = -1; down = 0; mods = 0;
    REQUIRE(input_trygetrawkey() == -1 && input_is_pressed(KEY_CTRL) == 0);
    REQUIRE(input_get_modifiers() == 0);
    /* Absent pointer/query/value/void and reserved slot, from real generation. */
    REQUIRE(kapi_reserved_nosys() == OS32_ERR_NOSYS);
    const char *drive = wrap_path_get_drive();
#ifdef PC98_INPUT
    REQUIRE(drive && drive[0] == 'A' && wrap_ime_is_active() == 1);
    REQUIRE(wrap_mouse_available() == 1);
    wrap_gfx_shutdown(); REQUIRE(shutdowns == 1);
#else
    REQUIRE(drive == 0 && wrap_mouse_available() == 0);
    REQUIRE(wrap_ime_is_active() == OS32_ERR_NOSYS); /* not a ZERO_SLOTS query */
    wrap_gfx_shutdown(); REQUIRE(shutdowns == 0);
#endif
    return failures;
}
void _start(void)
{
    int rc = input_kapi();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
#else
#include "kernel/irq/tick.h"
volatile u32 tick_count;
static u32 irq_state = 1;
u32 irq_save(void) { u32 old = irq_state; irq_state = 0; return old; }
void irq_restore(u32 old) { irq_state = old; }
u32 ppc_irq_save(void) { return irq_save(); }
void ppc_irq_restore(u32 old) { irq_restore(old); }
void _enable(void) { irq_state = 1; }
void _halt(void) {}
void ppc_enable(void) { irq_state = 1; }
int ppc_irq_enabled(void) { return irq_state; }
volatile u32 kctx_irq_depth;
static unsigned int naps;
void ppc_cpu_nap(void) { ++naps; }
#include IDLE_SOURCE
void _idle(void) { arch_cpu_idle(); }
static unsigned int work_calls;
void idle_work_run(void);
void _idle(void);
void idle_wait_step(void) { if (!ppc_irq_enabled()) return; idle_work_run(); _idle(); }
void idle_work_run(void) { ++work_calls; }
int tick_register(unsigned int slot, tick_fn fn) { (void)slot; (void)fn; return 0; }
#include PROVIDER_SOURCE

static int time3(void)
{
    static const u32 inputs[] = { 0, 3, 0xfffffffdu, 0xffffffffu };
    static const u32 expected[] = { 0, 3, 0xfffffffdu, 0xffffffffu };
    unsigned int i;
    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        tick_count = inputs[i];
        if (link_platform_now() != expected[i]) return 1;
    }
    return 0;
}
#ifdef MPIC_SOURCE
#include "platform/powermac/mpic.h"
#include "arch/ppc/ppc_trap.h"
static u32 regs[PM_MPIC_WINDOW_MAX / 4];
static unsigned eois, deliveries;
u32 pm_mpic_bus_read(u32 address, int little)
{ (void)little; return regs[address / 4]; }
void pm_mpic_bus_write(u32 address, u32 value, int little)
{ (void)little; regs[address / 4] = value; if (address == 0x200b0) ++eois; }
void ppc_trap_set_handler(u32 vector, void (*fn)(struct ppc_frame *))
{ (void)vector; (void)fn; }
#include MPIC_SOURCE
static void delivered(struct ppc_frame *frame) { (void)frame; ++deliveries; }
static int unchanged(struct irq_claim *c)
{ return c->line == 17 && c->native_token == 18 && c->deliver == 19 && c->needs_finish == 20; }
#define VERIFY(expr) do { if (!(expr)) return __LINE__; } while (0)
static int irq1(void)
{
    struct irq_claim c = {17, 18, 19, 20}, other = {17, 18, 19, 20};
    VERIFY(platform_irq_claim(0, &c) < 0 && unchanged(&c));
    regs[0x1000 / 4] = 3U << 16;
    regs[0x200a0 / 4] = PM_MPIC_SPURIOUS;
    VERIFY(pm_mpic_init(0, PM_MPIC_WINDOW_MAX, PM_MPIC_MMIO_BE) == 0);
    eois = 0;
    VERIFY(platform_irq_claim(0, 0) < 0 && unchanged(&c));
    VERIFY(platform_irq_claim(PM_MPIC_SPURIOUS, &c) < 0 && unchanged(&c));
    VERIFY(platform_irq_claim(4, &c) < 0 && unchanged(&c));
    VERIFY(platform_irq_claim(2, &other) == 1);
    VERIFY(other.line == 2 && other.native_token == 2 && other.deliver == 1 && other.needs_finish == 1);
    VERIFY(platform_irq_claim(1, &c) < 0 && unchanged(&c));
    platform_irq_complete(0);
    c = other; c.native_token = 1; platform_irq_complete(&c);
    c = other; c.needs_finish = 0; platform_irq_complete(&c);
    VERIFY(eois == 0);
    platform_irq_complete(&other); platform_irq_complete(&other);
    VERIFY(eois == 1);
    pm_mpic_set_ipi_handler(delivered);
    pm_mpic_external(0);
    VERIFY(deliveries == 0 && eois == 1);
    regs[0x200a0 / 4] = 4;
    pm_mpic_external(0);
    VERIFY(deliveries == 0 && eois == 2);
    regs[0x200a0 / 4] = PM_MPIC_IPI_VECTOR;
    VERIFY(platform_irq_claim(PM_MPIC_IPI_VECTOR, &c) == 1);
    VERIFY(c.line == PM_MPIC_IPI_VECTOR && c.native_token == PM_MPIC_IPI_VECTOR && c.deliver == 1 && c.needs_finish == 1);
    platform_irq_complete(&c);
    pm_mpic_external(0);
    VERIFY(deliveries == 1 && eois == 4);
    return 0;
}
#endif
#ifdef IDLE_SOURCE
static int sync2(void)
{
    irq_state = 0;
    _idle();
    if (!irq_state || naps != 0) return 2;
    /* Expectation origin: contract f6980232 (AC-SYNC-2) — an EE=1 _idle rests
     * the CPU until the next interrupt: PPC calls the nap primitive once. */
    _idle();
    if (!irq_state || naps != 1) return 5;
    irq_state = 0;
    link_platform_idle();
    if (irq_state || work_calls != 0) return 3;
    irq_state = 1;
    link_platform_idle();
    if (!irq_state || work_calls != 1) return 3;
    link_platform_idle();
    if (!irq_state || work_calls != 2) return 4;
    return 0;
}
#endif
void _start(void)
{
    int rc = time3();
#ifdef IDLE_SOURCE
    if (!rc) rc = sync2();
#endif
#ifdef MPIC_SOURCE
    if (!rc) rc = irq1();
#endif
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
#endif
