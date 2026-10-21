#ifndef KDEBUG_GFINIT_EXEC_H
#define KDEBUG_GFINIT_EXEC_H
#include "gfinit_decode.h"
#define GF_INIT_EXEC_US 60000000U
#define GF_INIT_WAIT_CHUNK_US 1000U
struct gfinit_exec_ops {
    /* Checked monotonic microseconds of execution, excluding measured UART
     * time. Error (including an unavailable calibrated clock) stops RUN. */
    int (*clock_us)(void *opaque, u32 *value);
    /* Wait at least us, with a finite hardware polling limit. */
    int (*wait_us)(void *opaque, u32 us);
};
struct gfinit_exec_frame { u32 pc, ctx, count; };
struct gfinit_exec {
    const u8 *rom;
    u32 rom_size, main_id;
    const struct gfinit_exec_ops *ops;
    void *opaque;
    u32 steps, wait_us, elapsed_us, fail_pc;
    u32 started, read_value, read_pc, read_ctx, calls, repeats;
    struct gfinit_exec_frame call[GF_INIT_CALL_DEPTH];
    struct gfinit_exec_frame repeat[GF_INIT_REPEAT_DEPTH];
};
/* A trial_step operation: execute exactly the expected main entry of a bound,
 * immutable decoded plan. The owner retains ROM/plan and handles cleanup via
 * trial_step; no direct bus or PLL callback bypasses recover authorization.
 * PLL operations are ordinary authorized internal plan nodes. */
int gfinit_exec_run(struct recover *r, void *arg);
#endif
