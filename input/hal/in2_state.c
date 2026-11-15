#include "in2_int.h"
#include "io.h"
#include "legacy_guest.h"
#include "os32_kapi_shared.h"

static u8 held[IN2_MAX_DEV][128], buttons[IN2_MAX_DEV];

u8 input_buttons(void)
{
    unsigned int flags = irq_save();
    u8 mask = 0;
    for (unsigned int i = 0; i < IN2_MAX_DEV; ++i) mask |= buttons[i];
    irq_restore(flags);
    return mask;
}

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
        u8 old = held[e.src - 1][e.sc];
        held[e.src - 1][e.sc] = e.down;
        input_merge_key(&e, old, origin);
    } else {
        if (kind != INPUT_KIND_MOUSE || e.down) goto out;
        switch (e.type) {
        case INPUT_EVENT_BUTTON:
            if (e.x || e.y || e.sc) goto out;
            e.sc = buttons[e.src - 1] ^ e.buttons;
            buttons[e.src - 1] = e.buttons;
            if (!e.sc) { rc = 0; goto out; }
            break;
        case INPUT_EVENT_ABS:
            if (e.x < 0 || e.y < 0) goto out;
            /* fall through */
        case INPUT_EVENT_MOVE:
            if (e.sc || e.buttons) goto out;
            break;
        case INPUT_EVENT_WHEEL:
            if (e.y || e.sc || e.buttons) goto out;
            break;
        default: goto out;
        }
        if (!legacy_active() && input_emit_in2) input_emit_in2(&e);
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
        kind == INPUT_KIND_KBD && held[src - 1][sc]) {
        InputEvent e = { .src = src, .generation = generation, .sc = sc, .down = 1 };
        input_merge_key(&e, 1, origin);
    }
    irq_restore(flags);
}

void input_state_release(u8 src, u16 generation)
{
    /* Called before the validated source slot is returned, with IRQs masked.
     * Capture already carries the producer's sampled ms; release samples now.
     * The clock port is absent only in this unconnected stage. */
    InputEvent e = { .src = src, .generation = generation, .type = INPUT_EVENT_KEY,
                     .time_ms = in2_time_ms ? in2_time_ms() : 0 };
    for (unsigned int sc = 0; sc < 128; ++sc) {
        if (!held[src - 1][sc]) continue;
        e.sc = sc;
        input_capture(&e);
    }
    e.type = INPUT_EVENT_BUTTON;
    e.sc = 0;
    if (buttons[src - 1]) input_capture(&e);
}
