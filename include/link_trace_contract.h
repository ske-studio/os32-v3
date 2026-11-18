#ifndef LINK_TRACE_CONTRACT_H
#define LINK_TRACE_CONTRACT_H

#include "types.h"

enum link_trace_event {
    LINK_TRACE_RESPONSE_DROP,
    LINK_TRACE_SILENCE,
    LINK_TRACE_RESYNC,
    LINK_TRACE_INVALIDATE
};

struct link_trace_rx {
    u32 rid, length, status, tick;
    u16 sess, epoch;
    u8 flags, state;
    const char *reason;
};

struct link_trace_call {
    struct link_trace_rx response, control;
    u32 rid, blocked_by, rejects, plan_tick, tx_tick;
    int first_reject, tx_rc;
    u32 received, length;
    u16 sess, epoch, credit;
    u8 up, owns_ring, planned, tried, sent;
    const char *read_wait;
};

/* Called with link exclusion held. Implementations only collect data. */
void link_trace_event(enum link_trace_event event, struct link_trace_rx detail);
void link_trace_call_snapshot(i32 handle, struct link_trace_call *trace);

#endif
