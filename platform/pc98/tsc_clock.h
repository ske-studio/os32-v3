#ifndef PC98_TSC_CLOCK_H
#define PC98_TSC_CLOCK_H
#include "types.h"

struct tsc_epoch { u32 lo, hi, us_lo, us_hi, mult, shift; };
int pc98_pit_read_us(u32 *lo, u32 *hi); /* Caller holds IF=0. */
int pc98_tsc_init(void);
void pc98_tsc_calibrate_work(void);
int pc98_tsc_read_us(u32 *lo, u32 *hi); /* Preserves IF and failed outputs. */
int pc98_tsc_ready(void);
/* Pure fixed point operations; fractional remainder belongs to the epoch. */
unsigned long long tsc_scale(unsigned long long delta, u32 mult, u32 shift,
                             u32 fraction, u32 *remainder);
int tsc_coefficient(unsigned long long cycles, u32 us, u32 *mult, u32 *shift);
#endif
