#include "in2_export.h"

void in2_export(const InputEvent *event, u16 seq, IN2_Event *out)
{
    *out = (IN2_Event){0};
    out->time_ms = event->time_ms;
    out->seq = seq;
    out->dev = event->src;
    out->type = event->type;
    out->flags = event->flags;

    switch (event->type) {
    case INPUT_EVENT_KEY:
        out->u.key.sc = event->sc;
        out->u.key.down = event->down;
        out->u.key.mods = event->buttons;
        break;
    case INPUT_EVENT_MOVE:
        out->u.move.dx = event->x;
        out->u.move.dy = event->y;
        break;
    case INPUT_EVENT_BUTTON:
        out->u.btn.down = event->buttons;
        out->u.btn.changed = event->sc;
        break;
    case INPUT_EVENT_WHEEL:
        out->u.wheel.delta = event->x;
        break;
    case INPUT_EVENT_PADBTN:
        out->u.pad.buttons = (u16)event->x;
        out->u.pad.changed = (u16)event->y;
        break;
    case INPUT_EVENT_ABS:
        out->u.abs.x = (u16)event->x;
        out->u.abs.y = (u16)event->y;
        break;
    case INPUT_EVENT_LOST:
        out->u.lost.reason = event->sc;
        /* Any nonzero upper half exceeds the public 16-bit count. */
        out->u.lost.count = event->y ? U16_MAX : (u16)event->x;
        break;
    }
}
