#ifndef TIME_SPLASH_MATH_H
#define TIME_SPLASH_MATH_H

#include "types.h"

/* PIT counts decrease; pending is the IRQ0 IRR bit at each snapshot. */
struct TimeSplashSample { u32 count, pending; };
struct TimeSplashMathInput {
    struct TimeSplashSample start, end;
    u32 reload;
    unsigned long long elapsed_min, elapsed_max; /* PIT cycles, inclusive */
};
struct TimeSplashMathResult {
    u32 wraps;
    long long tick_adjust; /* wraps + start.pending - end.pending */
};
/* Succeed only for a unique wrap count in the elapsed interval. */
int time_splash_math(const struct TimeSplashMathInput *input,
                     struct TimeSplashMathResult *result);
#endif
