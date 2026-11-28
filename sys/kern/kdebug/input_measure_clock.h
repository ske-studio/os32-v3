#ifndef OS32_KDEBUG_INPUT_MEASURE_H
#define OS32_KDEBUG_INPUT_MEASURE_H

#include "sys/include/input_measure.h"
#include "kdebug_resident_contract.h"
#include "sys/kern/io_work.h"

struct input_measure_stamp { u32 lo; int valid; };

static inline struct input_measure_stamp input_measure_start(void)
{
    struct input_measure_stamp stamp = {0};
    u32 hi;
    stamp.valid = kdebug.state == KDEBUG_RUNNING &&
                  !io_clock_us(&stamp.lo, &hi);
    return stamp;
}

static inline void input_measure_finish(struct input_measure_stamp start, u32 op)
{
    u32 lo, hi;
    if (start.valid && !io_clock_us(&lo, &hi)) {
        u32 elapsed = lo - start.lo;
        kdebug.ops.dbg_call(op, &elapsed, sizeof(elapsed));
    }
}

#endif
