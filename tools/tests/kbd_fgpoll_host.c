/* F2/F2i: compile the real keyboard with a foreground/device boundary.
 * No drawing or real keyboard input is needed to advance the wait loops. */
#include "types.h"
#include "io.h"

static void host_halt(void);
#define _halt() host_halt()
#include "kbd_source.c"

static unsigned polls, halts, violations;
static int pending, serial_byte = -1, injected = -1, wait_mode;
volatile u32 tick_count;
int rshell_active;

static void say(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(c, label) do { if (!(c)) { say("FAIL " label "\n"); return 1; } } while (0)

void geforce_foreground_poll(void)
{
    polls++;
    if (!_irq_enabled()) violations++;
}

static void host_halt(void)
{
    /* Every empty CUI wait must poll before halting, including later laps. */
    if (!kbd_gui_mode && polls != halts + 1) violations++;
    tick_count += wait_mode == 2 ? KBD_RSHELL_TIMEOUT_SEC * PIT_HZ / 2 : 1;
    if (++halts == 3) {
        if (wait_mode == 1) {
            kbd_buf[0] = 0x1d61;
            kbd_count = 1;
        } else {
            pending = 1;
        }
    }
    /* Bound a broken wait without relying on a timeout for mutation RED. */
    if (halts > 4) {
        say("FAIL wait did not terminate\n");
        __asm__ volatile("int $0x80" : : "a"(1), "b"(1));
        __builtin_unreachable();
    }
}

int ring3_wait_pending(void) { return pending; }
int exec_park_poll(u32 now) { (void)now; return 0; }
int exec_park_kbd(void) { return 0; }
int serial_trygetchar(void) { int ch = serial_byte; serial_byte = -1; return ch; }
u32 kbd_inject_pending(void) { return injected >= 0; }
int kbd_inject_take(u8 *out)
{
    if (injected < 0) return 0;
    *out = (u8)injected;
    injected = -1;
    return 1;
}

static void fresh(void)
{
    polls = halts = violations = 0;
    pending = rshell_active = kbd_gui_mode = wait_mode = 0;
    serial_byte = injected = -1;
    kbd_head = kbd_count = 0;
    _enable();
}

static int test(void)
{
    fresh();
    CHECK(kbd_getchar_wait(1) == -1, "CUI getchar abort");
    CHECK(halts == 3 && polls == 4 && !violations, "CUI getchar every lap before halt");
    fresh();
    CHECK(kbd_getkey_wait(1) == -1, "CUI getkey abort");
    CHECK(halts == 3 && polls == 4 && !violations, "CUI getkey every lap before halt");
    fresh();
    wait_mode = 1;
    CHECK(kbd_getchar_wait(0) == 'a' && halts == 3 && polls == 4 && !violations,
          "noninterruptible getchar wakes on key");
    fresh();
    wait_mode = 1;
    CHECK(kbd_getkey_wait(0) == 0x1d61 && halts == 3 && polls == 3 && !violations,
          "noninterruptible getkey wakes on key");

    fresh();
    CHECK(kbd_trygetchar() == -1 && polls == 1, "empty trychar polls");
    CHECK(kbd_trygetkey() == -1 && polls == 2, "empty trykey polls");
    kbd_buf[0] = 0x1d61;
    kbd_count = 1;
    CHECK(kbd_trygetchar() == 'a' && polls == 3, "ready trychar polls and preserves ASCII");
    kbd_buf[1] = 0x1d61;
    kbd_count = 1;
    CHECK(kbd_trygetkey() == 0x1d61 && polls == 4, "ready trykey polls and preserves scan");
    rshell_active = 1;
    serial_byte = 's';
    CHECK(kbd_trygetchar() == 's' && polls == 5, "serial trychar polls");
    serial_byte = 't';
    CHECK(kbd_trygetkey() == 't' && polls == 6, "serial trykey polls");
    CHECK(!halts && !violations, "try calls never halt or disable IRQ");

    fresh();
    rshell_active = 1;
    serial_byte = 'r';
    CHECK(kbd_getchar_wait(1) == 'r' && polls == 1 && !halts, "rshell ready wait");
    fresh();
    rshell_active = 1;
    wait_mode = 2;
    /* Unsigned elapsed ticks must also work across the PIT counter wrap. */
    tick_count = U32_MAX - PIT_HZ;
    CHECK(kbd_getchar_wait(0) == ' ' && polls == 2 && halts == 2 && !violations,
          "rshell timeout preserves elapsed ticks across wrap");

    fresh();
    kbd_gui_mode = 1;
    CHECK(kbd_trygetchar() == -1 && kbd_trygetkey() == -1 && !polls,
          "GUI empty try keeps park path");
    injected = 'g';
    CHECK(kbd_trygetchar() == 'g' && !polls, "GUI injected try");
    pending = 1;
    CHECK(kbd_getchar_wait(1) == -1 && kbd_getkey_wait(1) == -1 && !polls,
          "GUI wait keeps park path");
    say("GREEN keyboard CUI foreground waits and tries\n");
    return 0;
}

void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}

/* Device work is covered by ne2k_bh_host; this fixture supplies no callbacks. */
void idle_work_run(void) { }
