#include "rx_work.h"
#include "nic.h"
#include "clock_source.h"
#include "io.h"

u32 net_rx_clock_failures;
static int rx_work_clock(u32 *now)
{
    u32 hi;
    unsigned int flags = irq_save();
    int ok = platform_clock_read_us(now, &hi) == 0;
    irq_restore(flags);
    if (!ok) net_rx_clock_failures++;
    return ok;
}

void rx_work_begin(struct rx_work_budget *b)
{
    b->start = b->used = 0;
    b->max_frames = NET_RX_SCAN_MAX;
    b->limit_us = rx_work_clock(&b->start) ? NET_RX_WORK_LIMIT_US : 0;
}

int rx_work_take(struct rx_work_budget *b)
{
    u32 now;
    if (b->used >= b->max_frames) return 0;
    if (b->limit_us && rx_work_clock(&now) &&
        (u32)(now - b->start) >= b->limit_us) return 0;
    b->used++;
    return 1;
}
