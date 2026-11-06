/* C3: real client/protocol, one virtual SfsIo, C1 boundary only is stubbed. */
#include "fs/serialfs/serialfs.h"
#include "hostsvc_fixture.h"
#include "os32_kapi_shared.h"
#define REQUIRE(e) do { if (!(e)) return __LINE__; } while (0)
static SfsClient client;
static u8 rx[SFS_BUFFER_FRAME], first[SFS_BUFFER_FRAME];
static u32 pos, end, sends, first_len, stale, ensured, ensure_delay;
static int mode, put_delay, get_delay;
static u32 bounded_sends, passed_deadline, second_send;
/* modes: 0 normal, 1 silence, 2 continuous stray, 3 unknown sid,
 * 4 short response, 5 delayed until resend, 6 wrong nonce, 7 bad version. */
static int put(void *ctx, const u8 *buf, u32 n)
{
    (void)ctx;
    SfsDec dec;
    SfsFrame f;
    sfs_dec_reset(&dec);
    dec.max_payload = SFS_LAN_MAX_PAYLOAD;
    int ready = 0;
    for (u32 i = 0; i < n; i++) ready = sfs_dec_feed(&dec, buf[i], &f);
    if (ready != 1) __builtin_trap();
    if (!sends) {
        first_len = n;
        for (u32 i = 0; i < n; i++) first[i] = buf[i];
    } else {
        if (first_len != n) __builtin_trap();
        for (u32 i = 0; i < n; i++) if (first[i] != buf[i]) __builtin_trap();
    }
    if (sends == 1) second_send = hs_now();
    sends++;
    hs_advance(put_delay);
    int fail;
    if (hs_failure_take("sfs.reply", &fail)) return fail;
    if (mode == 1 || (mode == 5 && sends == 1)) return 0;
    u8 payload[SFS_HELLO_RSP_LEN] = {0};
    u16 len = SFS_STATUS_LEN;
    u8 type = f.type | SFS_T_RESP;
    u32 sid = f.sid;
    if (f.type == SFS_T_HELLO) {
        sid = 77;
        len = sizeof(payload);
        sfs_put16(payload + SFS_HELLO_RSP_VER, mode == 7 ? 0 : SFS_VERSION);
        sfs_put16(payload + SFS_HELLO_RSP_MAX, SFS_MAX_PAYLOAD);
        sfs_put32(payload + SFS_HELLO_RSP_NONCE,
                  sfs_get32(f.payload + SFS_HELLO_REQ_NONCE) + (mode == 6));
    } else sfs_put32(payload, (u32)OS32_ERR_NOTFOUND);
    if (mode == 3) type = SFS_T_ERR;
    if (mode == 4) len = 1;
    end = sfs_encode(rx, type, sid, f.seq + (mode == 2), payload, len);
    pos = 0;
    return 0;
}
static int get(void *ctx)
{
    (void)ctx;
    hs_advance(get_delay);
    if (pos < end) return rx[pos++];
    if (mode == 2) { hs_advance(1); return 0x44; }
    return -1;
}
static u32 now(void *ctx) { (void)ctx; return hs_now(); }
static void idle(void *ctx) { (void)ctx; hs_advance(1); }
static int can_wait(void *ctx) { (void)ctx; return hs_if(); }
static int put_until(void *ctx, const u8 *buf, u32 n, u32 deadline)
{
    bounded_sends++;
    passed_deadline = deadline;
    if ((i32)(hs_now() - deadline) >= 0) return OS32_ERR_TIMEOUT;
    if ((u32)put_delay >= deadline - hs_now()) {
        hs_advance(deadline - hs_now());
        return OS32_ERR_TIMEOUT;
    }
    return put(ctx, buf, n);
}
const SfsIo hs_sfs_io = {put, get, now, idle, can_wait, put_until};
static const SfsIo uart_io = {put, get, now, idle, can_wait, 0};
int sfs_ensure_session(u32 deadline)
{
    ensured = deadline;
    hs_advance(ensure_delay);
    return 0;
}
const SfsClient *sfs_persistent_client(void) { return &client; }
void sfs_persistent_stale(void) { stale++; }
static void reset(u32 start)
{
    hs_reset(start);
    sfs_client_init(&client, &hs_sfs_io, 0, 0);
    client.sid = 77;
    sends = pos = end = stale = ensured = ensure_delay = 0;
    mode = put_delay = get_delay = 0;
    bounded_sends = passed_deadline = second_send = 0;
}
static int suite(void)
{
    u8 data[32];
    struct SfsReply reply = {data, sizeof(data), 99};
    reset(0);
    REQUIRE(sfs_request_deadline(SFS_T_WRITE, 0, 0, &reply, 100) == 0);
    REQUIRE(ensured == 100 && sends == 1 && reply.length == 4);
    REQUIRE((i32)sfs_get32(data) == OS32_ERR_NOTFOUND);
    u8 saved = data[0];
    struct SfsReply other = {data + 8, 4, 0};
    first_len = sends = 0; /* a new request may use the next seq */
    REQUIRE(sfs_client_call_deadline(&client, SFS_T_STAT, 0, 0, &other, 100) == 0);
    REQUIRE(data[0] == saved && client.seq == 2);
    reset(0); reply.capacity = 3; data[0] = 0xa5;
    REQUIRE(sfs_client_call_deadline(&client, SFS_T_STAT, 0, 0, &reply, 100) == OS32_ERR_NOSPC);
    REQUIRE(reply.length == 4 && data[0] == 0xa5);
    reply.capacity = sizeof(data);
    for (u32 pass = 0; pass < 4; pass++) {
        reset(pass == 1 ? 0xfffffff0u : 0);
        mode = pass == 2 ? 2 : 1;
        if (pass == 3) hs_fail("sfs.reply", -1);
        u32 start = hs_now();
        REQUIRE(sfs_request_deadline(SFS_T_WRITE, 0, 0, &reply, start + 100) == OS32_ERR_HOSTDOWN);
        REQUIRE(hs_now() - start == 100 && sends == 10 && bounded_sends == 10 && reply.length == 0);
        REQUIRE(client.dead == SFS_DEAD_NONE && !stale && client.seq == 1);
        for (u32 i = 0; i < 4; i++) {
            sends = 0;
            REQUIRE(sfs_request_deadline(SFS_T_STAT, 0, 0, &reply, hs_now() + 100) == OS32_ERR_HOSTDOWN);
            REQUIRE(sends == 10 && client.dead == SFS_DEAD_NONE);
        }
        mode = 0; sends = 0;
        REQUIRE(sfs_request_deadline(SFS_T_STAT, 0, 0, &reply, hs_now() + 100) == 0);
    }
    reset(0); mode = 5;
    REQUIRE(sfs_client_call_deadline(&client, SFS_T_WRITE, 0, 0, &reply, 100) == 0);
    REQUIRE(sends == 2 && hs_now() == 10 && client.seq == 1);
    reset(0); mode = 5; put_delay = 50;
    REQUIRE(sfs_client_call_deadline(&client, SFS_T_WRITE, 0, 0, &reply, 200) == 0);
    REQUIRE(sends == 2 && second_send == 60 && hs_now() == 110);
    reset(0); mode = 3;
    REQUIRE(sfs_request_deadline(SFS_T_WRITE, 0, 0, &reply, 100) == OS32_ERR_STALE);
    REQUIRE(stale == 1 && sends == 1 && !client.sid && reply.length == 0);
    reset(0); mode = 4;
    REQUIRE(sfs_request_deadline(SFS_T_STAT, 0, 0, &reply, 100) == OS32_ERR_IO);
    reset(0); ensure_delay = 90; mode = 1;
    REQUIRE(sfs_request_deadline(SFS_T_STAT, 0, 0, &reply, 100) == OS32_ERR_HOSTDOWN);
    REQUIRE(hs_now() == 100 && sends == 1);
    reset(0); put_delay = 100;
    REQUIRE(sfs_request_deadline(SFS_T_STAT, 0, 0, &reply, 100) == OS32_ERR_HOSTDOWN);
    /* Previous counterexample: ensure consumes 90, TX wants another 100. */
    reset(0); ensure_delay = 90; put_delay = 100;
    REQUIRE(sfs_request_deadline(SFS_T_WRITE, 0, 0, &reply, 100) == OS32_ERR_HOSTDOWN);
    REQUIRE(hs_now() == 100 && ensured == 100 && passed_deadline == 100);
    REQUIRE(bounded_sends == 1 && sends == 0 && client.dead == SFS_DEAD_NONE);
    reset(0); put_delay = 100;
    REQUIRE(sfs_client_hello_deadline(&client, 123, 10) == OS32_ERR_HOSTDOWN);
    REQUIRE(hs_now() == 10 && bounded_sends == 1 && sends == 0);
    reset(0); put_delay = 100;
    REQUIRE(sfs_client_oneway_deadline(&client, SFS_T_BYE, 0, 0, 10) == OS32_ERR_TIMEOUT);
    REQUIRE(hs_now() == 10 && passed_deadline == 10 && sends == 0);
    reset(0);
    REQUIRE(sfs_client_oneway_deadline(&client, SFS_T_BYE, 0, 0, 0) == OS32_ERR_TIMEOUT);
    REQUIRE(!bounded_sends && !sends);
    reset(0); hs_set_if(0);
    REQUIRE(sfs_client_oneway_deadline(&client, SFS_T_BYE, 0, 0, 10) == OS32_ERR_IO);
    REQUIRE(!bounded_sends && !sends);
    reset(0);
    REQUIRE(sfs_client_oneway_deadline(&client, SFS_T_BYE, 0, 0, 10) == 0);
    REQUIRE(bounded_sends == 1 && sends == 1 && first[SFS_OFF_SEQ] == 0);
    reset(0); hs_fail("sfs.reply", OS32_ERR_BUSY);
    REQUIRE(sfs_client_oneway_deadline(&client, SFS_T_BYE, 0, 0, 10) == OS32_ERR_IO);
    reset(0); client.io = &uart_io;
    REQUIRE(sfs_client_oneway_deadline(&client, SFS_T_BYE, 0, 0, 10) == 0);
    REQUIRE(sends == 1 && !bounded_sends);
    reset(0); client.io = &uart_io;
    REQUIRE(sfs_client_hello_deadline(&client, 123, 100) == 0 && sends == 1);
    reset(0); client.io = &uart_io;
    REQUIRE(sfs_client_call_deadline(&client, SFS_T_STAT, 0, 0, &reply, 100) == 0);
    reset(0); get_delay = 100;
    REQUIRE(sfs_request_deadline(SFS_T_STAT, 0, 0, &reply, 100) == OS32_ERR_HOSTDOWN);
    reset(0); hs_set_if(0);
    REQUIRE(sfs_request_deadline(SFS_T_STAT, 0, 0, &reply, 100) == OS32_ERR_IO && !sends);
    reset(0);
    REQUIRE(sfs_client_call_deadline(&client, SFS_T_STAT, 0, 0, &reply, 0) == OS32_ERR_HOSTDOWN && !sends);
    reset(0); client.sid = 0; client.max_payload = SFS_LAN_MAX_PAYLOAD;
    REQUIRE(sfs_client_hello_deadline(&client, 123, 100) == 0);
    REQUIRE(client.sid == 77 && client.seq == 0 && client.max_payload == SFS_MAX_PAYLOAD);
    reset(0); mode = 6;
    REQUIRE(sfs_client_hello_deadline(&client, 123, 100) == OS32_ERR_HOSTDOWN);
    reset(0); mode = 7;
    REQUIRE(sfs_client_hello_deadline(&client, 123, 100) == OS32_ERR_IO);
    reset(0); mode = 1;
    REQUIRE(sfs_client_hello_deadline(&client, 123, 100) == OS32_ERR_HOSTDOWN && hs_now() == 100);
    return 0;
}
void _start(void)
{
    int rc = suite();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
