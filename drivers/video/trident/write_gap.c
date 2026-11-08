/* Calibrated integer write spacing, recalculated only when the value changes. */
#include "trident.h"

void tri_write_gap_set(struct trident_device *d, u32 ns)
{
    /* A u32 product fits u64. Divide without a module libgcc dependency;
     * saturate only when the requested loop count cannot fit u32. */
    unsigned long long value = (unsigned long long)d->write_gap_lpt * ns;
    u32 loops = 0;
    if (value >= ((unsigned long long)TRI_WRITE_GAP_TICK_NS << 32))
        loops = (u32)-1;
    else for (int bit = 31; bit >= 0; --bit) {
        unsigned long long part = (unsigned long long)TRI_WRITE_GAP_TICK_NS << bit;
        if (value >= part) {
            value -= part;
            loops |= 1u << bit;
        }
    }
    d->write_gap_ns = ns;
    d->write_gap_loops = loops;
}
