#ifndef CLOCK_SOURCE_H
#define CLOCK_SOURCE_H
#include "types.h"
struct clock_info { u32 tick_hz, counter_hz; int ready; };
int platform_clock_init(u32 tick_hz);
void platform_clock_info(struct clock_info *out);
/* Called with IF=0; outputs point only to kernel locals. */
int platform_clock_read_us(u32 *lo, u32 *hi);
#endif
