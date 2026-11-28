#include "v86_fm_timer.h"

enum v86_fm_step v86_fm_step(struct v86_fm_timer *t, SND2_FmQ *q,
                             unsigned long long now)
{
    if (t->overloaded) return V86_FM_OVERLOAD;
    if (t->guest_due) return V86_FM_GUEST;
    if (!t->target_valid) {
        t->target = now;
        t->target_valid = 1;
    }
    if (t->period_us && t->next_csm <= t->target) {
        /* AV §9-3 kind 2 is a CSM key-on at the scheduled expiry. */
        if (!snd2_fmq_push(q, 2, 0, 0, 0, (u32)t->next_csm))
            return V86_FM_DRAIN;
        t->position = t->next_csm;
        t->next_csm += t->period_us;
        if (t->position < t->target) return V86_FM_MORE;
    }
    t->position = t->target;
    t->target_valid = 0;
    t->guest_due = 1;
    return V86_FM_GUEST;
}

void v86_fm_guest_completed(struct v86_fm_timer *t)
{
    t->guest_due = 0;
}

void v86_fm_pump_window(struct v86_fm_timer *t, unsigned long long now)
{
    unsigned long long lag = now - t->position;
    if (lag > t->previous_lag) ++t->growing_windows;
    else t->growing_windows = 0;
    if (t->growing_windows >= 2) t->overloaded = 1;
    t->previous_lag = lag;
}
