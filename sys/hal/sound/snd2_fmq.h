#ifndef SND2_FMQ_H
#define SND2_FMQ_H
#include "av_contract.h"

/* Zero-initialized per session. Only the foreground producer/drainer runs. */
typedef struct {
    SND2_FmEv events[SND2_FMQ_LEN];
    u32 head, count, sequence;
} SND2_FmQ;

int snd2_fmq_push(SND2_FmQ *q, u8 kind, u8 bank, u8 reg, u8 val, u32 at_us);
u32 snd2_fmq_drain(SND2_FmQ *q, SND2_FmEv *out, u32 max);
#endif
