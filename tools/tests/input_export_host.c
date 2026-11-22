#include "in2_export.h"
#include "kbd.h"

static const struct {
    InputEvent input;
    u8 type;
    IN2_EventData payload;
} cases[] = {
    {{.type=INPUT_EVENT_KEY, .sc=KEY_STOP, .down=1, .buttons=SHIFT_CTRL},
     IN2_EV_KEY, {.key={KEY_STOP, 1, SHIFT_CTRL, 0}}},
    {{.type=INPUT_EVENT_MOVE, .x=-32768, .y=32123, .flags=INPUT_EVENT_SEAMLESS},
     IN2_EV_MOVE, {.move={-32768, 32123}}},
    {{.type=INPUT_EVENT_BUTTON, .buttons=5, .sc=6}, IN2_EV_BUTTON, {.btn={5, 6, 0}}},
    {{.type=INPUT_EVENT_WHEEL, .x=-257}, IN2_EV_WHEEL, {.wheel={-257, 0}}},
    {{.type=INPUT_EVENT_PADBTN, .x=(i16)0x8001, .y=0x4002},
     IN2_EV_PADBTN, {.pad={0x8001, 0x4002}}},
    {{.type=INPUT_EVENT_ABS, .x=12345, .y=32767}, IN2_EV_ABS, {.abs={12345, 32767}}},
    {{.type=INPUT_EVENT_LOST, .sc=INPUT_LOST_OVERFLOW, .x=1234},
     IN2_EV_LOST, {.lost={IN2_LOST_OVERFLOW, 1234}}},
    {{.type=INPUT_EVENT_LOST, .sc=INPUT_LOST_OVERFLOW, .x=-1},
     IN2_EV_LOST, {.lost={IN2_LOST_OVERFLOW, 65535}}},
    {{.type=INPUT_EVENT_LOST, .sc=INPUT_LOST_OVERFLOW, .y=1},
     IN2_EV_LOST, {.lost={IN2_LOST_OVERFLOW, 65535}}},
    {{.type=INPUT_EVENT_LOST, .sc=IN2_LOST_FOCUS}, IN2_EV_LOST, {.lost={IN2_LOST_FOCUS, 0}}},
    {{.type=INPUT_EVENT_LOST, .sc=IN2_LOST_EXCL}, IN2_EV_LOST, {.lost={IN2_LOST_EXCL, 0}}},
};

static int test(void)
{
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        InputEvent e = cases[i].input;
        e.src = e.type == INPUT_EVENT_LOST ? 0 : IN2_MAX_DEV;
        e.generation = 10 + i;
        e.time_ms = 0xf1234567 + i;
        u16 seq = (u16)(65535 + i);
        IN2_Event out;
        __builtin_memset(&out, 0xa5, sizeof(out));
        in2_export(&e, seq, &out);
        if (out.type != cases[i].type || out.dev != e.src || out.seq != seq ||
            out.time_ms != e.time_ms || out.rsv ||
            out.flags != (out.type == IN2_EV_MOVE ? IN2_EVF_SEAMLESS : 0) ||
            __builtin_memcmp(&out.u, &cases[i].payload, sizeof(out.u)))
            return i + 1;
    }
    return 0;
}

void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
