#include "in2_int.h"
#include "io.h"
#include "legacy_guest.h"
#include "os32_kapi_shared.h"

int input_capture(const InputEvent *event)
{
    unsigned int flags = irq_save();
    u8 kind, origin;
    int rc = OS32_ERR_INVAL;
    if (!event) goto out;
    InputEvent e = *event;
    if (!input_src_info(e.src, e.generation, &kind, &origin)) goto out;
    if (e.flags & ~INPUT_EVENT_SEAMLESS) goto out;
    if (e.flags && e.type != INPUT_EVENT_MOVE) goto out;
    if (e.type == INPUT_EVENT_KEY) {
        if (kind != INPUT_KIND_KBD || e.sc >= 128 || e.down > 1 ||
            e.x || e.y || e.buttons) goto out;
        u8 old = input_held_key(e.src, e.sc);
        input_merge_key(&e, old, origin);
    } else {
        if (kind != INPUT_KIND_MOUSE || e.down) goto out;
        switch (e.type) {
        case INPUT_EVENT_BUTTON:
            if (e.x || e.y || e.sc) goto out;
            if (!pointer_button(&e)) { rc = 0; goto out; }
            break;
        case INPUT_EVENT_ABS:
            if (e.x < 0 || e.y < 0 || e.sc || e.buttons) goto out;
            pointer_absolute(e.x, e.y);
            break;
        case INPUT_EVENT_MOVE:
            if (e.sc || e.buttons) goto out;
            mouse_ext_move(e.x, e.y, e.flags & INPUT_EVENT_SEAMLESS);
            break;
        case INPUT_EVENT_WHEEL:
            if (e.y || e.sc || e.buttons) goto out;
            if (!legacy_active()) mouse_ext_wheel(e.x);
            break;
        default: goto out;
        }
        if (!legacy_active()) input_emit_in2(&e);
    }
    rc = 0;
out:
    irq_restore(flags);
    return rc;
}

void input_repeat(u8 src, u16 generation, u8 sc)
{
    unsigned int flags = irq_save();
    u8 kind, origin;
    if (sc < 128 && input_src_info(src, generation, &kind, &origin) &&
        kind == INPUT_KIND_KBD && input_held_key(src, sc)) {
        InputEvent e = { .src = src, .generation = generation, .sc = sc, .down = 1 };
        input_merge_key(&e, 1, origin);
    }
    irq_restore(flags);
}

void input_state_release(u8 src, u16 generation)
{
    /* Called before the validated source slot is returned, with IRQs masked.
     * Capture already carries the producer's sampled ms; release samples now. */
    InputEvent e = { .src = src, .generation = generation, .type = INPUT_EVENT_KEY,
                     .time_ms = in2_time_ms() };
    for (unsigned int sc = 0; sc < 128; ++sc) {
        if (!input_held_key(src, sc)) continue;
        e.sc = sc;
        input_capture(&e);
    }
    e.type = INPUT_EVENT_BUTTON;
    e.sc = 0;
    if (pointer_source_buttons(src)) input_capture(&e);
}
