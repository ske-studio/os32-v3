#include "in2_int.h"
#include "sys/hal/time/tick_clock.h"

u32 in2_time_ms(void)
{
    return tick_clock_ms();
}
