#include "../../input/hal/in2_int.h"
#include "../../input/hal/keymap.h"
#include "bootlog.h"
#include "kbd.h"
#include "io.h"

static u8 kbd_native_src;
static u16 kbd_native_generation;
static u8 kbd_native_keymap;

void kbd_hal_init(u8 locks)
{
    unsigned int flags = irq_save();
    if (kbd_native_src) input_src_release(kbd_native_src, kbd_native_generation);
    input_legacy_keymap_set(&pc98_jp_backend);
    int src = pc98_input_keymap_register(&kbd_native_generation);
    kbd_native_keymap = src > 0;
    if (!kbd_native_keymap) {
        static const char message[] = "kbd: keymap registration failed; using legacy sc input\n";
        bootlog_push(message, sizeof(message) - 1);
        /* Registration releases a failed bind. Keep this source unbound. */
        src = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_NATIVE, &kbd_native_generation);
    }
    kbd_native_src = src > 0 ? (u8)src : 0;
    if (kbd_native_src) input_native_locks(kbd_native_src, locks);
    irq_restore(flags);
}

void kbd_hal_capture(u8 scancode)
{
    if (kbd_native_keymap) {
        KmKey key = { .src = kbd_native_src, .generation = kbd_native_generation,
            .keycode = scancode & SCANCODE_KEY, .down = !(scancode & SCANCODE_BREAK) };
        (void)input_capture_key(&key);
        return;
    }
    InputEvent event = { .time_ms = in2_time_ms(), .src = kbd_native_src,
        .generation = kbd_native_generation, .type = INPUT_EVENT_KEY,
        .sc = scancode & SCANCODE_KEY, .down = !(scancode & SCANCODE_BREAK) };
    (void)input_capture(&event);
}
