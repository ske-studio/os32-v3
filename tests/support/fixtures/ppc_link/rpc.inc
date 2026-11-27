/* In the fixture copy of link.c, host handles index link_h directly. Keep
 * recent CALLs across close and tag each slot with the complete wire rid. */
enum { RPC_WDATA = 1, RPC_RELEASE = 2, RPC_EXPIRED = 4 };
struct rpc_sample {
    u32 rid, deadline, detected, wdata, release, seen;
};
static volatile struct rpc_sample rpc_trace[16];

void rpc_opened(int handle, u32 deadline)
{
    u32 rid = link_h[handle].rid;
    rpc_trace[rid % 16] = (struct rpc_sample){ .rid = rid, .deadline = deadline };
}

void rpc_expired(int handle, u32 now)
{
    u32 rid = link_h[handle].rid;
    volatile struct rpc_sample *s = &rpc_trace[rid % 16];
    if (s->rid == rid) {
        s->detected = now;
        s->seen |= RPC_EXPIRED;
    }
}

static void rpc_sent(u32 rid, u32 tick, u32 event)
{
    volatile struct rpc_sample *s = &rpc_trace[rid % 16];
    if (s->rid != rid || (s->seen & event)) return;
    if (event == RPC_WDATA) s->wdata = tick;
    else s->release = tick;
    s->seen |= event;
}
