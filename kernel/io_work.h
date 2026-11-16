#ifndef IO_WORK_H
#define IO_WORK_H

#include "types.h"

#define IO_PASS_US 500u

/* Internal raw-count state: callers use begin/take, never access members. */
struct io_slice {
    u32 start_count, limit_count, max_units, units;
    u32 clock_source, clock_generation;
};
struct io_desc {
    u32 id, default_budget_us, budget_min_us, budget_max_us, max_unit_us;
    u8 default_tier; u8 reserved[3];
    int (*step)(void *state, struct io_slice *slice);
    void *state;
};
enum { IO_DONE = 0, IO_MORE = 1, IO_WAIT = 2 }; /* Negative values: errors. */
int io_register(const struct io_desc *d); /* Resident provider; FG0/FG1, also inside io_dispatch (runs from the next round). */
int io_unregister(u32 id); /* FG1; BUSY while running; no references on success. */
void io_request(u32 id); /* IRQ/FG0/FG1: mark only, never call the provider. */
int io_clock_us(u32 *lo, u32 *hi);
/* Initialize raw budget/token/units; 0 on success, negative on failure. */
int io_slice_begin(struct io_slice *s, u32 budget_us, u32 max_units);
/* Narrow remaining time/units, preserving the clock token; never widen. */
int io_slice_limit(struct io_slice *s, u32 budget_us, u32 max_units);
int io_slice_take(struct io_slice *s); /* Check raw count/units before a unit. */
int io_dispatch(u32 budget_us); /* FG1 only; nested calls return 0. */

/* Providers guard step reentry themselves; nested calls return IO_MORE. */
#endif
