/* Internal PC-98 timer boundary. Values are temporary, never owned state. */
#ifndef KERNEL_PLATFORM_TIMER_H
#define KERNEL_PLATFORM_TIMER_H

#include "types.h"

struct platform_clock_value {
    unsigned long hz;
    u8 is_8mhz;
};
struct platform_timer_sample {
    unsigned int count;
    int p1, p2;
};

/* Boot foreground, before paging: read the original BIOS clock byte once. */
void platform_clock_read(struct platform_clock_value *out);
/* Boot foreground, IF=0: mode2 -> LSB -> MSB; caller publishes setup first. */
void platform_timer_program(unsigned int reload);
/* Caller IF=0: IRR -> latch/lo/hi -> IRR, without tick or interpolation. */
void platform_timer_sample(struct platform_timer_sample *out);

#endif /* KERNEL_PLATFORM_TIMER_H */
