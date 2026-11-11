/* Convert calibrated loops per 10 ms to a saturated write gap. */
#include "sync_int.h"

#define GFXSYNC_TICK_NS 10000000u

u32 gfxsync_gap_loops(u32 loops_per_tick, u32 ns)
{
    unsigned long long value = (unsigned long long)loops_per_tick * ns;
    u32 loops = 0;

    if (value >= ((unsigned long long)GFXSYNC_TICK_NS << 32))
        return (u32)-1;
    for (int bit = 31; bit >= 0; --bit) {
        unsigned long long part = (unsigned long long)GFXSYNC_TICK_NS << bit;
        if (value >= part) {
            value -= part;
            loops |= 1u << bit;
        }
    }
    return loops;
}
