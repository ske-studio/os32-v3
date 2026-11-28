#ifndef V86_FM_TIMER_H
#define V86_FM_TIMER_H
#include "snd2_fmq.h"

/* Foreground control only. All times are microseconds since session begin. */
struct v86_fm_timer {
    unsigned long long position, target, next_csm, previous_lag;
    u32 period_us, growing_windows;
    int target_valid, guest_due, overloaded;
};
enum v86_fm_step { V86_FM_MORE, V86_FM_DRAIN, V86_FM_GUEST, V86_FM_OVERLOAD };
enum v86_fm_step v86_fm_step(struct v86_fm_timer *t, SND2_FmQ *q,
                             unsigned long long now);
void v86_fm_guest_completed(struct v86_fm_timer *t);
/* Call once at each completed 768-output-frame pump window. */
void v86_fm_pump_window(struct v86_fm_timer *t, unsigned long long now);
#endif
