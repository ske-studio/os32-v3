#include "snd2_fmq.h"

int snd2_fmq_push(SND2_FmQ *q, u8 kind, u8 bank, u8 reg, u8 val, u32 at_us)
{
    if (q->count == SND2_FMQ_LEN) return 0;
    unsigned tail = (q->head + q->count) % SND2_FMQ_LEN;
    q->events[tail] = (SND2_FmEv){kind, bank, reg, val, at_us, ++q->sequence};
    ++q->count;
    return 1;
}

u32 snd2_fmq_drain(SND2_FmQ *q, SND2_FmEv *out, u32 max)
{
    u32 count = max < q->count ? max : q->count;
    for (u32 i = 0; i < count; ++i) {
        out[i] = q->events[q->head];
        q->head = (q->head + 1) % SND2_FMQ_LEN;
    }
    q->count -= count;
    return count;
}
