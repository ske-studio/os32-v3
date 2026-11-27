#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sys/fs/serialfs/serialfs_persistent.c"

enum scenario { IDLE_EXPIRES, GET_EXPIRES, PUT_EXPIRES, EARLY_TIMEOUT, REPLY, RENEW_EXPIRES };
static struct {
    enum scenario scenario;
    u32 tick, deadline, len, calls, cancels, replies, pos, reply_len;
    int pending;
    u8 frame[SFS_BUFFER_FRAME], reply[SFS_BUFFER_FRAME];
} wire;

void host_alive_snapshot(struct HostAlive *out) { *out = (struct HostAlive){0}; }
void boot_token_get(u8 out[16]) { memset(out, 0, 16); }

static u32 now(void *ctx) { assert(ctx == &wire); return wire.tick; }
static int can_wait(void *ctx) { return ctx == &wire; }
static void idle(void *ctx) { assert(ctx == &wire); wire.tick++; }
static int put(void *ctx, const u8 *buf, u32 n) { abort(); }

static int put_until(void *ctx, const u8 *buf, u32 n, u32 deadline)
{
    assert(ctx == &wire && n <= sizeof(wire.frame) && !wire.pending);
    wire.calls++;
    wire.pending = 1;
    wire.len = n;
    wire.deadline = deadline;
    memcpy(wire.frame, buf, n);
    if (wire.scenario == PUT_EXPIRES) wire.tick = deadline;
    if (wire.scenario == EARLY_TIMEOUT) return OS32_ERR_TIMEOUT;
    if (buf[SFS_OFF_TYPE] == SFS_T_HELLO && wire.scenario == REPLY) {
        u8 payload[SFS_HELLO_RSP_LEN] = {0};
        sfs_put16(payload + SFS_HELLO_RSP_VER, SFS_VERSION);
        sfs_put16(payload + SFS_HELLO_RSP_MAX, SFS_MAX_PAYLOAD);
        memcpy(payload + SFS_HELLO_RSP_NONCE, buf + SFS_HDR_LEN + SFS_HELLO_REQ_NONCE, 4);
        wire.reply_len = sfs_encode(wire.reply, SFS_T_HELLO | SFS_T_RESP, 7, 0, payload, sizeof(payload));
        wire.pos = 0;
        wire.replies++;
    }
    return 0;
}

static int get(void *ctx)
{
    assert(ctx == &wire);
    if (wire.scenario == GET_EXPIRES) wire.tick = wire.deadline;
    if (wire.pos == wire.reply_len) return -1;
    int b = wire.reply[wire.pos++];
    if (wire.pos == wire.reply_len) wire.pending = 0;
    return b;
}

static void cancel(void *ctx, const u8 *buf, u32 n, u32 deadline)
{
    assert(ctx == &wire && wire.pending && n == wire.len && deadline == wire.deadline);
    assert(memcmp(buf, wire.frame, n) == 0);
    wire.pending = 0;
    wire.cancels++;
    if (wire.scenario == RENEW_EXPIRES) {
        assert(buf[SFS_OFF_TYPE] == SFS_T_BYE);
        wire.tick = deadline;
    }
}

static SfsIo io = {put, get, now, idle, can_wait, put_until, cancel};

static void exchange(enum scenario scenario, u32 start, int synchronous)
{
    memset(&wire, 0, sizeof(wire));
    wire.scenario = scenario;
    wire.tick = start;
    u32 deadline = start + 2;
    SfsClient client;
    struct SfsReply out = {0};
    io.cancel = synchronous ? NULL : cancel;
    sfs_client_init(&client, &io, &wire, 115200);
    client.sid = 7;
    int rc = sfs_client_call_deadline(&client, SFS_T_STAT, NULL, 0, &out, deadline);
    assert(rc == OS32_ERR_HOSTDOWN && wire.tick == deadline && client.timeouts == 1);
    assert(wire.calls == 1 && wire.deadline == deadline);
    if (!synchronous && wire.pending) {
        puts("expired exchange left pending CALL");
        exit(1);
    }
    assert(wire.cancels == !synchronous);
}

int main(void)
{
    for (int sync = 0; sync <= 1; sync++) {
        exchange(IDLE_EXPIRES, 10, sync);
        exchange(PUT_EXPIRES, 10, sync);
        exchange(GET_EXPIRES, 0xffffffffu, sync);
    }
    io.cancel = cancel;
    memset(&wire, 0, sizeof(wire));
    SfsClient *c = &resident.client;
    sfs_client_init(c, &io, &wire, 115200);
    assert(sfs_client_hello_deadline(c, 1, 0) == OS32_ERR_HOSTDOWN);
    assert(!wire.calls && !wire.cancels);
    for (int i = 0; i < 2; i++) {
        wire.scenario = i ? EARLY_TIMEOUT : PUT_EXPIRES;
        wire.tick = 0;
        assert(sfs_client_oneway_deadline(c, SFS_T_BYE, NULL, 0, 4) == OS32_ERR_TIMEOUT);
        assert(!wire.pending && wire.cancels == i + 1 && wire.tick == (i ? 0 : 4));
    }
    config.io = &io;
    config.io_ctx = &wire;
    enabled = 1;
    wire.scenario = REPLY;
    assert(sfs_ensure_session(10) == 0 && c->sid == 7 && wire.cancels == 2);
    for (int i = 0; i < 2; i++) {
        c->seq = SFS_PERSISTENT_SEQ_LIMIT + 1;
        wire.scenario = i ? RENEW_EXPIRES : REPLY;
        u32 before = wire.calls;
        int rc = sfs_ensure_session(10);
        assert(rc == (i ? OS32_ERR_HOSTDOWN : 0) && !wire.pending);
        assert(wire.calls - before == (i ? 1 : 2) && wire.cancels == 3 + i);
    }
    wire.scenario = REPLY;
    assert(sfs_ensure_session(20) == 0 && wire.replies == 3 && !wire.pending);
    return 0;
}
