/* No CPU video WC provider on this target. */
#include "video_wc.h"
int arch_video_wc_active(void) { return 0; }
int arch_video_wc_eligible(u32 fb, u32 mapped, u32 extent, u32 regs, u32 reg_bytes)
{
    (void)fb; (void)mapped; (void)extent; (void)regs; (void)reg_bytes;
    return 0;
}
int arch_video_wc_start(u32 base, u32 bytes)
{
    (void)base; (void)bytes;
    return 0;
}
int arch_video_wc_stop(void) { return 0; }
