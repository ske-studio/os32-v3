#include "in2_int.h"
#include "io.h"
#include "kbd.h"

static InputEvent events[IN2_RING_CAPACITY];
static u16 sequences[IN2_RING_CAPACITY];
static u16 head, count, next_seq;
static u32 pending, dropped;

static void append(const InputEvent *e)
{
    unsigned int i = (head + count) % IN2_RING_CAPACITY;
    events[i] = *e;
    sequences[i] = next_seq++;
    ++count;
}

static i16 add_sat(i16 a, i16 b)
{
    int n = (int)a + b;
    if (n > 32767) n = 32767;
    if (n < -32768) n = -32768;
    return (i16)n;
}

int in2_ring_push(const InputEvent *e)
{
    unsigned int flags = irq_save();
    if (pending && count < IN2_RING_CAPACITY) {
        InputEvent lost = {0};
        lost.time_ms = e->time_ms;
        lost.type = INPUT_EVENT_LOST;
        lost.sc = INPUT_LOST_OVERFLOW;
        lost.x = (i16)(u16)pending;
        lost.y = (i16)(u16)(pending >> 16);
        pending = 0;
        append(&lost);
    }
    if (count && e->type == INPUT_EVENT_MOVE) {
        unsigned int i = (head + count - 1) % IN2_RING_CAPACITY;
        if (events[i].type == INPUT_EVENT_MOVE && events[i].src == e->src) {
            events[i].x = add_sat(events[i].x, e->x);
            events[i].y = add_sat(events[i].y, e->y);
            events[i].time_ms = e->time_ms;
            irq_restore(flags);
            return 1;
        }
    }
    if (count == IN2_RING_CAPACITY) {
        ++pending;
        ++dropped;
        if (e->type == INPUT_EVENT_KEY && e->sc == KEY_STOP && e->down &&
            (e->buttons & SHIFT_CTRL)) {
            unsigned int i = (head + count - 1) % IN2_RING_CAPACITY;
            events[i] = *e;
            sequences[i] = next_seq++;
            irq_restore(flags);
            return 1;
        }
        irq_restore(flags);
        return 0;
    }
    append(e);
    irq_restore(flags);
    return 1;
}

int in2_ring_pop(InputEvent *e, u16 *seq)
{
    unsigned int flags = irq_save();
    if (!count) { irq_restore(flags); return 0; }
    *e = events[head];
    *seq = sequences[head];
    head = (head + 1) % IN2_RING_CAPACITY;
    --count;
    irq_restore(flags);
    return 1;
}

u32 in2_ring_dropped(void)
{
    unsigned int flags = irq_save();
    u32 result = dropped;
    irq_restore(flags);
    return result;
}
