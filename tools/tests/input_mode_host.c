#include "../../input/hal/legacy_ring.c"
volatile int kbd_stop_suppressed;
static int resets;
void appslot_poll_yield_reset(void) { resets++; }
#include "mode_source.inc"
/* Oracle copied from 32e9fe0e:drivers/input/kbd.c, with fixture names. */
static volatile int old_mode, old_stop;
static int old_cooked, old_raw, old_cd, old_rd, old_resets;
static void old_set(int on)
{
    int next = on ? 1 : 0;
    unsigned int flags;
    if (next == old_mode) return;
    flags = irq_save();
    old_mode = next;
    old_stop = 0;
    if (next) {
        old_cd = 0;
        old_rd = 0;
    }
    old_cooked = 0;
    old_raw = 0;
    irq_restore(flags);
    old_resets++;
}
static int probe(void)
{
    int modes[] = { 0, 1, 2, 0, 0, -1, 0 };
    for (unsigned i = 0; i < sizeof(modes)/sizeof(modes[0]); i++) {
        for (int n = 0; n < KBD_BUF_SIZE + 1; n++) {
            legacy_cooked_push(42); legacy_raw_push(1, 0, 0);
            if (old_cooked < KBD_BUF_SIZE) old_cooked++; else old_cd++;
            if (old_raw < KBD_BUF_SIZE) old_raw++; else old_rd++;
        }
        old_stop = kbd_stop_suppressed = 1;
        old_set(modes[i]); kbd_set_gui_mode(modes[i]);
        if (old_mode != kbd_gui_mode || old_stop != kbd_stop_suppressed ||
            old_cd != (int)legacy_cooked_dropped() || old_rd != (int)legacy_raw_dropped() ||
            old_cooked != legacy_cooked_pending() || old_raw != kbd_raw_count ||
            old_resets != resets || !_irq_enabled()) return 1;
    }
    return 0;
}
void _start(void) {
    _enable();
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
