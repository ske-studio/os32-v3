#ifdef INPUT_MEASURE_DIAG
#include "sys/kern/kdebug/input_measure_clock.h"
#endif
#include "input_work.h"
#include "in2_int.h"
#include "kbd.h"
#include "../../input/input_policy.h"
#include "sys/kern/io_work.h"
#include "io.h"

enum { REPEAT_SLOTS = 4 }; /* USB keyboard handles per machine. */

static struct repeat_slot {
    KmKey key;
    KmEvalResult result;
    u8 raw_due, mods, consumed;
    KmState state;
    u32 time_ms;
} slots[REPEAT_SLOTS];
static int registered;

void input_work_invalidate_locked(void)
{
    for (unsigned i = 0; i < REPEAT_SLOTS; ++i) slots[i].key.src = 0;
}

void input_work_release_locked(u8 src, int usage)
{
    for (unsigned i = 0; i < REPEAT_SLOTS; ++i)
        if (slots[i].key.src == src && (usage < 0 || slots[i].key.keycode == usage))
            slots[i].key.src = 0;
}

int input_work_repeat_locked(const KmKey *key, const KmEvalResult *result,
                             u8 raw_due, u8 mods, KmState state, u8 consumed, u32 time_ms)
{
    if (!registered) return 0;
    unsigned free_slot = REPEAT_SLOTS;
    for (unsigned i = 0; i < REPEAT_SLOTS; ++i) {
        if (slots[i].key.src == key->src) { free_slot = i; break; }
        if (!slots[i].key.src) free_slot = i;
    }
    if (free_slot < REPEAT_SLOTS) {
        slots[free_slot] = (struct repeat_slot){
            .key = *key, .result = *result, .raw_due = raw_due,
            .mods = mods, .consumed = consumed, .state = state, .time_ms = time_ms
        };
        io_request(IO_SVC_INPUT_REPEAT);
    }
    return 1;
}

int input_step(struct io_slice *slice)
{
    int limited = io_slice_limit(slice, INPUT_REPEAT_US, REPEAT_SLOTS);
    for (;;) {
        unsigned flags = irq_save();
#ifdef INPUT_MEASURE_DIAG
        struct input_measure_stamp stamp = input_measure_start();
#endif
        unsigned i = 0;
        while (i < REPEAT_SLOTS && !slots[i].key.src) ++i;
        if (i == REPEAT_SLOTS || limited < 0 || !io_slice_take(slice)) {
            irq_restore(flags);
            return i == REPEAT_SLOTS ? IO_DONE : IO_MORE;
        }
        struct repeat_slot slot = slots[i];
        slots[i].key.src = 0;
        KmSource source;
        if (!km_source_lookup(slot.key.src, slot.key.generation, &source) &&
            input_held_key(slot.key.src, slot.key.keycode)) {
            u8 sc = slot.result.compat_sc;
            if (kbd_gui_mode && slot.raw_due && !in2_excl_drop_raw(sc, 0) &&
                !legacy_stop_filter(sc, 0, slot.mods))
                ikm_capture_gui_key(&slot.key, &source, &slot.state, &slot.result,
                                    1, slot.consumed, slot.mods, slot.time_ms);
            km_legacy_compat(&slot.result);
            if (!input_capture_delivery(&slot.key, 1, &slot.result, slot.raw_due, slot.mods))
                input_emit_keymap(&slot.result, slot.key.down, slot.raw_due, slot.mods, 0);
        }
#ifdef INPUT_MEASURE_DIAG
        input_measure_finish(stamp, INPUT_MEASURE_FG1);
#endif
        irq_restore(flags);
    }
}

static int step(void *state, struct io_slice *slice)
{
    (void)state;
    return input_step(slice);
}

int input_work_register(void)
{
    const struct io_desc desc = {
        .id = IO_SVC_INPUT_REPEAT, .default_tier = 2, .default_budget_us = INPUT_REPEAT_US,
        .budget_min_us = INPUT_REPEAT_US, .budget_max_us = INPUT_REPEAT_US,
        .max_unit_us = 30, .step = step
    };
    int rc = io_register(&desc);
    if (!rc) registered = 1;
    return rc;
}
