#include "in2_int.h"
#include "kernel/time/tick_clock.h"
#include "kbd.h"

static int test(void)
{
    InputEvent e = {0}, out;
    u16 seq;
    e.type = INPUT_EVENT_KEY; e.src = 1;
    for (int i = 0; i < 128; ++i) if (!in2_ring_push(&e)) return 1;
    if (in2_ring_push(&e) || in2_ring_dropped() != 1) return 2;
    e.sc = KEY_STOP; e.down = 1; e.buttons = SHIFT_CTRL;
    if (!in2_ring_push(&e) || in2_ring_dropped() != 2) return 3;
    for (int i = 0; i < 128; ++i) {
        if (!in2_ring_pop(&out, &seq)) return 4;
        if (i == 127 && (out.sc != KEY_STOP || seq != 128)) return 5;
    }
    e.sc = 0; e.down = 0; e.buttons = 0;
    if (!in2_ring_push(&e) || !in2_ring_pop(&out, &seq) ||
        out.type != INPUT_EVENT_LOST || out.sc != INPUT_LOST_OVERFLOW || out.x != 2) return 6;
    if (!in2_ring_pop(&out, &seq)) return 7;
    e.type = INPUT_EVENT_MOVE; e.x = 7; e.y = -3; e.src = 1; e.time_ms = 1;
    in2_ring_push(&e); e.x = 5; e.time_ms = 2; in2_ring_push(&e);
    e.src = 2; in2_ring_push(&e);
    if (!in2_ring_pop(&out, &seq) || out.x != 12 || out.y != -6 || out.time_ms != 2) return 8;
    if (!in2_ring_pop(&out, &seq) || out.src != 2 || out.x != 5) return 9;
    e.type = INPUT_EVENT_KEY;
    for (unsigned int i = 0; i < 65536; ++i) {
        in2_ring_push(&e); if (!in2_ring_pop(&out, &seq)) return 10;
        if (i && seq != (u16)(i + 133)) return 11;
    }
    if (tick_clock_config(1, 100)) return 12;
    tick_clock_step(); if (in2_time_ms() != 10) return 13;
    if (tick_clock_config(1, 1000)) return 14;
    for (int i = 0; i < 10; ++i) tick_clock_step();
    if (in2_time_ms() != 10) return 15;
    e.type = INPUT_EVENT_MOVE; e.src = 2; e.generation = 1;
    e.x = 7; e.y = -3; e.time_ms = 20;
    if (!in2_ring_push(&e)) return 16;
    e.generation = 2; e.x = 5; e.y = 4; e.time_ms = 21;
    if (!in2_ring_push(&e)) return 17;
    if (!in2_ring_pop(&out, &seq) || out.src != 2 || out.generation != 1 ||
        out.x != 7 || out.y != -3 || out.time_ms != 20) return 18;
    if (!in2_ring_pop(&out, &seq) || out.src != 2 || out.generation != 2 ||
        out.x != 5 || out.y != 4 || out.time_ms != 21) return 19;
    if (in2_ring_pop(&out, &seq)) return 20;
    return 0;
}
#ifndef PPC_COMPILE
void _start(void) { int rc = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
#else
int input_ring_test(void) { return test(); }
#endif
