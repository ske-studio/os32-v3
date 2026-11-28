#include "sys/kern/kdebug/input_measure_clock.h"
#include "sys/kern/kdebug/input_measure.inc"

struct kdebug kdebug;
static u32 now, reads;
static int clock_error;

int io_clock_us(u32 *lo, u32 *hi)
{
    reads++;
    *lo = now;
    *hi = 0;
    return clock_error;
}

static i32 record(u32 op, void *buf, u32 size)
{
    return input_measure_record(op, buf, size);
}

static void sample(u32 op, u32 elapsed)
{
    struct input_measure_stamp stamp = input_measure_start();
    now += elapsed;
    input_measure_finish(stamp, op);
}

int probe(void)
{
    kdebug.ops.dbg_call = record;
    sample(INPUT_MEASURE_USB, 20);
    if (reads) return 1;
    kdebug.state = KDEBUG_RUNNING;
    now = 0xfffffff0;
    sample(INPUT_MEASURE_USB, 50);
    sample(INPUT_MEASURE_USB, 3);
    sample(INPUT_MEASURE_FG1, 7);
    clock_error = 1;
    sample(INPUT_MEASURE_FG1, 80);
    return input_measure_stats[0].count != 2 || input_measure_stats[0].max_us != 50 ||
           input_measure_stats[1].count != 1 || input_measure_stats[1].max_us != 7;
}
