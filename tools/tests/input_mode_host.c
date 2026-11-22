#define legacy_cooked_flush real_cooked_flush
#define legacy_raw_flush real_raw_flush
#define kbd_raw_purge_keep real_raw_purge_keep
#include "../../input/hal/legacy_ring.c"
#undef legacy_cooked_flush
#undef legacy_raw_flush
#undef kbd_raw_purge_keep
static int flush_irq_bad;
void legacy_cooked_flush(void) { if (_irq_enabled()) flush_irq_bad++; real_cooked_flush(); }
void legacy_raw_flush(void) { if (_irq_enabled()) flush_irq_bad++; real_raw_flush(); }
void kbd_raw_purge_keep(void)
{
    if (_irq_enabled()) flush_irq_bad++;
    real_raw_purge_keep();
}
volatile u32 tick_count;
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
static int stop_probe(void)
{
    kbd_gui_mode = 1; tick_count = 100; kbd_discard_stop();
    if (!kbd_stop_suppressed || !legacy_stop_filter(KEY_STOP, 0, SHIFT_CTRL)) return 1;
    if (legacy_stop_filter(KEY_STOP, 1, SHIFT_CTRL) || !kbd_stop_suppressed) return 1;
    tick_count += OS32_TICK_HZ / 2 - 1;
    if (!legacy_stop_filter(KEY_STOP, 0, SHIFT_CTRL)) return 1;
    tick_count++;
    if (legacy_stop_filter(KEY_STOP, 0, SHIFT_CTRL) || kbd_stop_suppressed) return 1;
    kbd_discard_stop(); legacy_stop_filter(KEY_SHIFT, 0, SHIFT_CTRL);
    if (!kbd_stop_suppressed) return 1;
    legacy_stop_filter(1, 0, SHIFT_CTRL);
    if (kbd_stop_suppressed) return 1;
    kbd_discard_stop(); kbd_stop_mouse_sample(1);
    if (kbd_stop_suppressed) return 1;
    kbd_discard_stop(); kbd_stop_mouse_sample(1);
    if (!kbd_stop_suppressed) return 1;
    kbd_set_gui_mode(0);
    const int rescue[] = { KEY_CTRL, KEY_GRPH, KEY_TAB, KEY_STOP };
    for (unsigned i = 0; i < sizeof(rescue)/sizeof(rescue[0]); i++) {
        legacy_raw_push(rescue[i], 0, SHIFT_CTRL);
        legacy_raw_push(1, 0, 0);
        legacy_raw_push(rescue[i], 1, SHIFT_GRPH);
    }
    legacy_cooked_push('x');
    legacy_cooked_push('y');
    kbd_discard_stop();
    for (unsigned i = 0; i < sizeof(rescue)/sizeof(rescue[0]); i++) {
        if (legacy_raw_take() != (rescue[i] | KBD_RAW_MAKE | (SHIFT_CTRL << KBD_RAW_MOD_SHIFT)) ||
            legacy_raw_take() != (rescue[i] | (SHIFT_GRPH << KBD_RAW_MOD_SHIFT))) return 1;
    }
    return kbd_stop_suppressed || legacy_raw_take() != -1 || legacy_cooked_take() != -1 ||
           flush_irq_bad || !_irq_enabled();
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
            old_resets != resets || flush_irq_bad || !_irq_enabled()) return 1;
    }
    return 0;
}
void _start(void) {
    _enable();
    int rc = probe() || stop_probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}

int in2_excl_drop_raw(u8 sc, int br) { (void)sc; (void)br; return 0; }
