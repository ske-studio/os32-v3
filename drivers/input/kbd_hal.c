#include "../../input/hal/in2_int.h"
#include "kbd.h"
#include "io.h"

static u8 kbd_native_src;
static u16 kbd_native_generation;

void kbd_hal_init(u8 locks)
{
    unsigned int flags = irq_save();
    if (kbd_native_src) input_src_release(kbd_native_src, kbd_native_generation);
    int src = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_NATIVE, &kbd_native_generation);
    kbd_native_src = src > 0 ? (u8)src : 0;
    if (kbd_native_src) input_native_locks(kbd_native_src, locks);
    irq_restore(flags);
}

void kbd_hal_capture(u8 scancode)
{
    InputEvent event = { .time_ms = in2_time_ms(), .src = kbd_native_src,
        .generation = kbd_native_generation, .type = INPUT_EVENT_KEY,
        .sc = scancode & SCANCODE_KEY, .down = !(scancode & SCANCODE_BREAK) };
    (void)input_capture(&event);
}
