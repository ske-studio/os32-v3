#ifndef KERNEL_TIME_TICK_CLOCK_H
#define KERNEL_TIME_TICK_CLOCK_H

#include "kernel_tick.h"

#define TICK_STEP_JIFFY 1u
#define TICK_CLOCK_MS_HZ 1000u
#define TICK_CLOCK_EINVAL (-1)

/* Configure num/den seconds per physical period at boot with IF/EE clear,
 * before changing the device. Invalid values leave all state unchanged.
 * Success resets phases and milliseconds, preserving raw and jiffy counts. */
int tick_clock_config(u32 period_num, u32 period_den);
/* With IRQs disabled, install one owner or pass NULL to release it. */
int tick_fast_set(void (*fn)(u32 now_ms));
/* PCM owns the intermediate (non-jiffy) slot while its DMA is active. */
void tick_pcm_set(void (*fn)(void));
/* Once per physical period, with IF/EE clear and IRQ depth >= 1.
 * Bit 0 signals a jiffy; before configuration every step advances one. */
u32 tick_clock_step(void);
/* Call with IF/EE clear to obtain coherent 64-bit snapshots. */
void tick_clock_raw(u32 *lo, u32 *hi);
void tick_clock_jiffy64(u32 *lo, u32 *hi);
/* Single-word read, permitted in any context. */
u32 tick_clock_ms(void);

#endif
