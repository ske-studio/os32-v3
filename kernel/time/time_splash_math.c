#include "time_splash_math.h"

int time_splash_math(const struct TimeSplashMathInput *input,
                     struct TimeSplashMathResult *result)
{
    unsigned long long first, periods, distance;
    u32 base, initial_wrap;

    if (!input || !result || input->reload == 0 ||
        input->start.count == 0 || input->start.count > input->reload ||
        input->end.count == 0 || input->end.count > input->reload ||
        input->start.pending > 1 || input->end.pending > 1 ||
        input->elapsed_min > input->elapsed_max)
        return -1;

    initial_wrap = input->start.count < input->end.count;
    base = initial_wrap ? input->reload - (input->end.count - input->start.count)
                        : input->start.count - input->end.count;
    periods = 0;
    if (input->elapsed_min > base) {
        distance = input->elapsed_min - base;
        periods = distance / input->reload;
        if (distance % input->reload)
            periods++;
    }
    if (periods > (unsigned long long)U32_MAX - initial_wrap)
        return -1;

    first = periods * input->reload + base;
    if (first < input->elapsed_min || first > input->elapsed_max ||
        input->elapsed_max - first >= input->reload)
        return -1;

    result->wraps = (u32)periods + initial_wrap;
    result->tick_adjust = (long long)result->wraps + input->start.pending -
                          input->end.pending;
    return 0;
}
