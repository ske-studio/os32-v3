#include "snd2.h"
#include "io.h"
#include "kernel/time/tick_clock.h"

extern i32 exec_sys_yield(void);

int snd2_stream_pos(u32 id, SND2_Pos *out)
{
    Snd2Device *d;
    unsigned flags = irq_save();
    int rc = snd2_owned_device(id, &d);
    if (!rc) {
        if (!out) rc = OS32_ERR_INVAL;
        else {
            const Snd2Stream *s = &d->stream;
            *out = (SND2_Pos){
                .size = sizeof(*out), .state = s->state,
                .played = snd2_seg_played_now(s), .written = s->written,
                .cap = s->cap, .underruns = s->underruns,
                .epoch = s->epoch, .time_ms = tick_clock_ms()
            };
        }
    }
    irq_restore(flags);
    return rc;
}

int snd2_stream_wait(u32 id, u32 frame, u32 timeout_ms)
{
    SND2_Pos pos;
    u32 begin = tick_clock_ms();
    for (;;) {
        int rc = snd2_stream_pos(id, &pos);
        if (rc) return rc;
        if ((i32)(frame - pos.written) > 0) return OS32_ERR_INVAL;
        if ((i32)(pos.played - frame) >= 0) return 0;
        if (pos.time_ms - begin >= timeout_ms) return OS32_ERR_TIMEOUT;
        exec_sys_yield();
    }
}
