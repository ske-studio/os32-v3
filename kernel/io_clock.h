#ifndef IO_CLOCK_H
#define IO_CLOCK_H

#include "types.h"

/* IO_TIER §5: raw-count clock provider for the I/O Tier (T1 x86, T2 PPC).
 * The provider is installed once its clock is ready; until then the Tier
 * reports OS32_ERR_NOSYS for budgets and io_clock_us. */
struct io_clock_ops {
    /* Monotonic raw count (TSC/PIT-with-period on x86, TB on PPC).
     * Differences are valid below half a wrap. 0, NOSYS or AGAIN. */
    int (*read)(u32 *count, u32 *source, u32 *generation);
    /* us -> raw count for that source/generation, rounded down, no 64-bit
     * division. AGAIN when source/generation no longer match. */
    int (*us_to_count)(u32 us, u32 source, u32 generation, u32 *count);
    /* AC-TIME-4 monotonic microseconds (io_clock_us forwards here). */
    int (*read_us)(u32 *lo, u32 *hi);
};

/* Boot FG0/FG1. NULL uninstalls (tests only). */
void io_clock_set_ops(const struct io_clock_ops *ops);

#endif
