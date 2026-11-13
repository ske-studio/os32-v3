#ifndef NET_RX_WORK_H
#define NET_RX_WORK_H
#include "types.h"
#define NET_RX_WORK_LIMIT_US 120u
struct rx_work_budget { u32 start, used, max_frames, limit_us; };
/* Clock failure falls back to the frame bound and is counted. */
extern u32 net_rx_clock_failures;
void rx_work_begin(struct rx_work_budget *b);
int rx_work_take(struct rx_work_budget *b);
#endif
