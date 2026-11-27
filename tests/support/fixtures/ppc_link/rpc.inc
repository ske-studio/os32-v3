#include "sys/fs/serialfs/sfs_proto.h"
#include "tests/support/fixtures/ppc_link/rpc.h"

struct rpc_sample {
    /* RPC_FIELDS */
};
static volatile struct rpc_sample rpc_trace[16], staging;
static volatile struct rpc_sample *client = &staging, *transport;
static u32 sequence, generation, start_tick, configured_deadline, foreground;
static struct rpc_identity {
    void *ctx;
    u32 len, deadline, generation;
    u8 data[SFS_BUFFER_FRAME];
} frame, accepted;

u32 rpc_start(u32 now, u32 width)
{
    ++generation;
    start_tick = now;
    configured_deadline = now + width;
    return configured_deadline;
}

void rpc_begin(u32 deadline)
{
    client = &staging;
    staging = (struct rpc_sample){ .deadline = deadline };
    if (generation && deadline == configured_deadline) {
        staging.generation = generation;
        staging.start = start_tick;
    }
}

static int matches(const struct rpc_identity *id, void *ctx, const u8 *buf,
                   u32 len, u32 deadline)
{
    if (!generation || generation != id->generation || ctx != id->ctx ||
        len != id->len || deadline != id->deadline) return 0;
    for (u32 i = 0; i < len; ++i)
        if (buf[i] != id->data[i]) return 0;
    return 1;
}

void rpc_frame(void *ctx, const u8 *buf, u32 len, u32 deadline)
{
    frame.generation = 0;
    if (!staging.generation || deadline != staging.deadline || len > sizeof(frame.data)) return;
    frame.ctx = ctx;
    frame.len = len;
    frame.deadline = deadline;
    frame.generation = generation;
    for (u32 i = 0; i < len; ++i) frame.data[i] = buf[i];
}

static struct rpc_sample admission;
void rpc_accept(void *ctx, const u8 *buf, u32 len, u32 deadline)
{
    transport = 0;
    if (!matches(&frame, ctx, buf, len, deadline)) return;
    accepted = frame;
    admission = staging;
    client = transport = &admission;
}

void rpc_opened(int handle)
{
    u32 rid = link_h[handle].rid;
    volatile struct rpc_sample *s = &rpc_trace[rid % 16];
    *s = (struct rpc_sample){0};
    if (!transport) return;
    *s = *transport;
    if (client == transport) client = s;
    transport = s;
    s->rid = rid;
    s->seen |= RPC_OPEN;
}

void rpc_poll(u32 tick, int can_wait)
{
    ++foreground;
    if (!transport) return;
    transport->poll_tick = tick;
    transport->poll_round = foreground;
    transport->can_wait = can_wait;
    if ((i32)(tick - transport->deadline) < 0) {
        transport->prior_poll = foreground;
        transport->prior_tick = tick;
        transport->prior_wait = can_wait;
    } else if (!transport->late_poll) {
        transport->late_poll = foreground;
        transport->late_tick = tick;
        transport->late_wait = can_wait;
    }
}

u32 rpc_compare(u32 now, u32 source)
{
    volatile struct rpc_sample *s = source == RPC_CLIENT ? client : transport;
    if (source == RPC_CLIENT) ++foreground;
    if (!s) return now;
    u32 event = ++sequence;
    if (source == RPC_CLIENT && !s->first_client_event && (i32)(now - s->deadline) >= 0) {
        s->first_client_event = event;
        s->first_client_tick = now;
    }
    if (!s->end_event) {
        if ((i32)(now - s->deadline) >= 0) {
            s->end = now;
            s->end_event = event;
            s->end_round = foreground;
            s->source = source;
        } else {
            s->prev = now;
            s->prev_event = event;
        }
    }
    return now;
}

void rpc_timeout(void *ctx, const u8 *buf, u32 len, u32 deadline)
{
    if (client != transport || !matches(&accepted, ctx, buf, len, deadline)) return;
    client->timeout_event = ++sequence;
    client->timeout_tick = link_platform_now();
    client->timeout_round = foreground;
}

static void rpc_reason(volatile struct rpc_sample *s, u32 reason)
{
    if (s->reason) return;
    s->reason = reason;
    s->reason_tick = link_platform_now();
}

void rpc_close(int handle, u32 reason)
{
    u32 rid = link_h[handle].rid;
    volatile struct rpc_sample *s = &rpc_trace[rid % 16];
    if (s->rid != rid || !(s->seen & RPC_OPEN)) return;
    rpc_reason(s, reason);
    ++s->closes;
    s->close_tick = link_platform_now();
    s->close_event = ++sequence;
    s->close_round = foreground;
}

void rpc_return(void *ctx, const u8 *buf, u32 len, u32 deadline)
{
    if (client != transport || !matches(&accepted, ctx, buf, len, deadline)) return;
    client->return_tick = link_platform_now();
    client->return_event = ++sequence;
}

static void rpc_sent(u32 rid, u32 tick, u32 event)
{
    volatile struct rpc_sample *s = &rpc_trace[rid % 16];
    if (s->rid != rid || !(s->seen & RPC_OPEN) || (s->seen & event)) return;
    if (event == RPC_WDATA) s->wdata = tick;
    else s->release = tick;
    s->seen |= event;
}
