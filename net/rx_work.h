#ifndef NET_RX_WORK_H
#define NET_RX_WORK_H
#include "types.h"
#include "kernel/io_work.h"
#define NET_RX_WORK_LIMIT_US 120u
struct rx_work_budget { struct io_slice local, *slice; u32 used, max_units; };
/* Clock failure starts no further units and is counted. */
extern u32 net_rx_clock_failures;
void rx_work_begin(struct rx_work_budget *b);
void rx_work_from_slice(struct rx_work_budget *b, struct io_slice *slice);
/* P10: share time with device work, cap demux units locally. */
void rx_work_from_slice_capped(struct rx_work_budget *b, struct io_slice *slice);
int rx_work_take(struct rx_work_budget *b);
#endif
