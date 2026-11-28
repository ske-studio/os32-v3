#include "v86_fm_timer.h"
#include "check.h"

void _start(void)
{
    static SND2_FmQ q;
    const unsigned long long start = 0xffffffffULL - 1;
    struct v86_fm_timer t = {.period_us = 1, .next_csm = start, .position = start};
    q.sequence = 0xfffffffeu;
    for (unsigned i = 0; i < SND2_FMQ_LEN; ++i)
        check(snd2_fmq_push(&q, 1, 1, 7, i, (u32)start), "fill queue");
    u32 last = q.sequence;
    check(v86_fm_step(&t, &q, start + 2) == V86_FM_DRAIN &&
          t.next_csm == start && q.sequence == last, "full queue issued");
    SND2_FmEv event;
    for (unsigned i = 0; i < SND2_FMQ_LEN; ++i)
        check(snd2_fmq_drain(&q, &event, 1) == 1 && event.seq == (u32)(0xffffffffu + i)
              && event.val == (u8)i && event.at_us == (u32)start, "queue order");
    for (unsigned i = 0; i < 3; ++i) {
        enum v86_fm_step rc = v86_fm_step(&t, &q, start + 10 + i);
        check(rc == (i == 2 ? V86_FM_GUEST : V86_FM_MORE), "fixed target");
        check(snd2_fmq_drain(&q, &event, 1) == 1 && event.seq == ++last &&
              event.kind == 2 && event.at_us == (u32)(start + i), "CSM order");
    }
    check(v86_fm_step(&t, &q, start + 20) == V86_FM_GUEST && !q.count, "guest progress");
    v86_fm_guest_completed(&t);
    check(v86_fm_step(&t, &q, start + 20) == V86_FM_MORE, "next target");
    v86_fm_pump_window(&t, t.position + 2);
    v86_fm_pump_window(&t, t.position + 1);
    v86_fm_pump_window(&t, t.position + 2);
    check(!t.overloaded, "transient overload");
    v86_fm_pump_window(&t, t.position + 3);
    check(v86_fm_step(&t, &q, start + 21) == V86_FM_OVERLOAD, "sustained overload");
    passed();
}
