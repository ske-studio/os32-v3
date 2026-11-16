#include "rx_work.h"
#include "nic.h"

u32 net_rx_clock_failures;
void rx_work_begin(struct rx_work_budget *b)
{
    b->used = 0;
    b->slice = 0;
    if (io_slice_begin(&b->local, NET_RX_WORK_LIMIT_US, NET_RX_SCAN_MAX) < 0)
        net_rx_clock_failures++;
    else b->slice = &b->local;
}

void rx_work_from_slice(struct rx_work_budget *b, struct io_slice *slice)
{
    b->used = 0;
    b->slice = slice;
}

int rx_work_take(struct rx_work_budget *b)
{
    if (!b->slice || !io_slice_take(b->slice)) return 0;
    b->used++;
    return 1;
}
