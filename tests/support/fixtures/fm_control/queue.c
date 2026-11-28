#include "snd2_fmq.h"
#include "check.h"

void _start(void)
{
    static SND2_FmQ q;
    static SND2_FmEv out[SND2_FMQ_LEN];
    q.sequence = 0xfffffffeu;
    for (u32 i = 0; i < SND2_FMQ_LEN; ++i)
        check(snd2_fmq_push(&q, 1, 1, 7, (u8)i, 0xfffffffeu + i), "queue fill");
    check(!snd2_fmq_push(&q, 1, 0, 1, 0, 2) &&
          q.sequence == (u32)(0xfffffffeu + SND2_FMQ_LEN), "full queue issued");
    check(!snd2_fmq_drain(&q, out, 0) && snd2_fmq_drain(&q, out, 2) == 2,
          "partial drain");
    check(out[0].seq == 0xffffffffu && !out[1].seq, "wrapped sequence");
    for (u32 i = 0; i < 2; ++i)
        check(snd2_fmq_push(&q, 2, 0, 0, 0, 512), "queue refill");
    check(snd2_fmq_drain(&q, out, SND2_FMQ_LEN + 1) == SND2_FMQ_LEN, "drain count");
    for (u32 i = 0; i < SND2_FMQ_LEN - 2; ++i)
        check(out[i].seq == i + 1 && out[i].at_us == i &&
              out[i].val == (u8)(i + 2), "queue order");
    check(out[510].seq + 1 == out[511].seq && out[510].at_us == out[511].at_us &&
          out[511].kind == 2, "same timestamp");
    passed();
}
