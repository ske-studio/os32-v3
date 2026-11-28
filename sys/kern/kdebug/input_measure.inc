#include "sys/include/input_measure.h"

/* Indexed by USB service / one consumed FG1 slot. Read with a debugger. */
static volatile struct {
    u32 count, max_us;
} input_measure_stats[2];

static i32 input_measure_record(u32 op, const void *buf, u32 size)
{
    if (!buf || size != sizeof(u32)) return OS32_ERR_INVAL;
    unsigned index = op == INPUT_MEASURE_FG1;
    u32 elapsed = *(const u32 *)buf;
    input_measure_stats[index].count++;
    if (elapsed > input_measure_stats[index].max_us)
        input_measure_stats[index].max_us = elapsed;
    return 0;
}
