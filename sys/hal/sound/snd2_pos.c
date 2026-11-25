#ifdef SND2_WRITER_DIAG
#include "snd_writer_probe.h"
#endif
#include "snd2.h"
#include "io.h"
#include "exec_wait_contract.h"
#include "sys/hal/time/tick_clock.h"

static int snapshot_locked(u32 id, SND2_Pos *out)
{
    Snd2Device *d;
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
    return rc;
}

int snd2_stream_pos(u32 id, SND2_Pos *out)
{
    unsigned flags = irq_save();
    int rc = snapshot_locked(id, out);
    irq_restore(flags);
    return rc;
}

int snd2_stream_wait(u32 id, u32 frame, u32 timeout_ms)
{
    SND2_Pos pos;
    u32 begin = exec_wait_begin(tick_clock_ms());
    int park = exec_wait_can_park();
#ifdef SND2_WRITER_DIAG
    sw_cancel();
#endif
    for (;;) {
        unsigned saved = irq_save();
        int rc = snapshot_locked(id, &pos);
        if (!rc) {
            if ((i32)(frame - pos.written) > 0) rc = OS32_ERR_INVAL;
            else if ((i32)(pos.played - frame) < 0) {
                if (pos.state == SND2_ST_PAUSE || ring3_wait_pending() ||
                    pos.time_ms - begin >= timeout_ms) rc = OS32_ERR_TIMEOUT;
                else if (park) {
                    irq_restore(saved);
                    exec_wait_park(begin);
                    continue;
                }
                else {
                    /* Interrupts stay masked from the snapshot through the
                     * atomic enable-and-halt. Any IRQ can change the result. */
#ifdef SND2_WRITER_DIAG
                    sw_sleep();
#endif
                    _idle();
                    irq_restore(saved);
                    continue;
                }
            }
        }
#ifdef SND2_WRITER_DIAG
        if (!rc) sw_ready();
        else sw_cancel();
#endif
        irq_restore(saved);
        return rc;
    }
}
