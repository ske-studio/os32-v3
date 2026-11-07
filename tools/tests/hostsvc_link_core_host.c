/* A8/A9 real link.c + B2 receiver, ILP32 without libc. */
#define LINK_HOST_TEST
#include "types.h"
volatile u32 tick_count;
static unsigned int test_if;
static int tx_result, tx_calls;
static u32 credit_bytes, tx_length;
static u8 tx_frame[1520];
unsigned int link_test_irq_save(void) { unsigned int f = test_if; test_if = 0; return f; }
void link_test_irq_restore(unsigned int f) { test_if = f; }
void link_test_idle(void) { }
#include "net/link.c"
void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *d = dst; const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
void *kmemset(void *dst, int v, u32 n)
{
    u8 *d = dst; while (n--) *d++ = v; return dst;
}
int net_tx(const void *frame, u32 n)
{
    tx_calls++;
    tx_length = n;
    kmemcpy(tx_frame, frame, n);
    return tx_result;
}
u32 net_rx_credit_bytes(void) { return credit_bytes; }
/* No LGY TX/credit/busy stubs: any remaining core call fails to link. */
static int nic_bound = 1;
int net_mac(u8 out[6])
{
    if (!nic_bound) return OS32_ERR_HOSTDOWN;
    kmemcpy(out, link_my_mac, ETH_ALEN);
    return 0;
}
static u8 rx_frame[1520];
static u32 rx_length;
static int rx_calls, mailbox_calls, mailbox_alive;
int net_rx_take(u16 protocol, void *out, u32 cap, u32 *len)
{
    rx_calls++;
    if (protocol != NET_RX_LINK) return OS32_ERR_INVAL;
    if (!rx_length) return OS32_ERR_AGAIN;
    if (cap < rx_length) return OS32_ERR_NOSPC;
    *len = rx_length;
    kmemcpy(out, rx_frame, rx_length);
    rx_length = 0;
    return 0;
}
#include "net/link_notify.c"
int host_mailbox_put(u32 generation, u16 service, u16 kind,
                     const void *data, u16 len, int alive)
{
    (void)generation; (void)service; (void)kind; (void)data; (void)len;
    mailbox_calls++; mailbox_alive = alive;
    return 0;
}
static void receive(u8 op, u8 flags, u32 seq, u32 ack, u32 rid,
                    const u8 *payload, u16 len)
{
    kmemcpy(rx_frame, link_my_mac, ETH_ALEN);
    kmemcpy(rx_frame + ETH_ALEN, link_peer_mac, ETH_ALEN);
    rx_frame[ETH_TYPE_OFF] = LINK_ETHERTYPE >> 8;
    rx_frame[ETH_TYPE_OFF + 1] = LINK_ETHERTYPE & 255;
    u8 *h = rx_frame + ETH_HLEN;
    kmemset(h, 0, LINK_HDR_LEN);
    h[LINK_OFF_OP] = op; h[LINK_OFF_FLAGS] = flags;
    le_write16(h + LINK_OFF_EPOCH, link_epoch);
    le_write16(h + LINK_OFF_SESS, link_sess);
    le_write32(h + LINK_OFF_SEQ, seq); le_write32(h + LINK_OFF_ACK, ack);
    le_write32(h + LINK_OFF_RID, rid); le_write16(h + LINK_OFF_LENGTH, len);
    if (len) kmemcpy(h + LINK_HDR_LEN, payload, len);
    rx_length = ETH_HLEN + LINK_HDR_LEN + len;
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int verify_rx(void);
static int verify_recovery(void);
static int verify(void)
{
    const u8 mac[6] = {2, 3, 4, 5, 6, 7};
    link_init(mac); /* No tick_register stub: init must not register a tick. */
    CHECK(link_ready && link_hs_due == 1);
    tx_result = OS32_ERR_AGAIN;
    link_tick();
    CHECK(tx_calls == 1 && link_hs_due == 1 && link_hs_tick == 0);
    tick_count = 9;
    tx_result = 0;
    link_tick();
    CHECK(tx_calls == 2 && link_hs_due == 0 && link_hs_tick == 9);
    CHECK(tx_length == LINK_ETH_HDR_LEN + LINK_HDR_LEN);
    CHECK(mac_eq(tx_frame + ETH_ALEN, mac));
    CHECK(tx_frame[ETH_TYPE_OFF] == (LINK_ETHERTYPE >> 8));
    CHECK(tx_frame[ETH_TYPE_OFF + 1] == (LINK_ETHERTYPE & 255));
    CHECK(tx_frame[LINK_ETH_HDR_LEN + LINK_OFF_OP] == LINK_OP_HELLO);
    tx_result = OS32_ERR_HOSTDOWN;
    CHECK(!link_send(LINK_OP_STATUS, 0, 0, 0, 1, 0, 0));
    credit_bytes = 100 * LINK_PAGE_BYTES + LINK_PAGE_BYTES - 1;
    CHECK(link_credit_pages() == LINK_STREAM_BUF / LINK_PAGE_BYTES - LINK_CREDIT_MARGIN);
    credit_bytes = (LINK_CREDIT_MARGIN + 3) * LINK_PAGE_BYTES + LINK_PAGE_BYTES - 1;
    CHECK(link_credit_pages() == 3);
    credit_bytes = LINK_CREDIT_MARGIN * LINK_PAGE_BYTES;
    CHECK(link_credit_pages() == 0);
    credit_bytes = 0;
    CHECK(link_credit_pages() == 0);

    link_hs_due = 0; link_state = LINK_S_UP; link_epoch = 3;
    link_hello_ok = 1;
    tick_count = U32_MAX - 49;
    nic_bound = 0;
    CHECK(link_host_open("TIME", 4, 1) == OS32_ERR_HOSTDOWN);
    nic_bound = 1;
    CHECK(link_host_open("TIME", 4, 1) == 0);
    CHECK(link_host_open("TIME", 4, 1) == 1);
    /* Expiry preserves the final RTO interval instead of cancelling it. */
    link_h[0].req_due = 0;
    link_h[0].last_tx_tick = tick_count;
    link_h[0].retries = LINK_TRIES - 1;
    link_h[1].req_acked = 1; link_h[1].req_due = 0;
    tick_count += 99;
    link_host_progress(1); /* Explicit hook; A9 receive wiring is outstanding. */
    link_h[0].last_tx_tick = tick_count; /* Avoid earlier unrelated RTO. */
    link_tick();
    CHECK(link_host_status(0, 0, 0, 1) == OS32_ERR_AGAIN);
    tick_count++;
    link_h[0].last_tx_tick = tick_count - LINK_RTO_TICKS;
    link_tick();
    CHECK(link_host_status(0, 0, 0, 1) == OS32_ERR_HOSTDOWN);
    CHECK(link_host_status(1, 0, 0, 1) == OS32_ERR_AGAIN);
    CHECK(link_epoch == 3 && link_state == LINK_S_UP && link_resyncs == 0);
    CHECK(link_h[0].req_due == 0 && link_h[0].want_status == 0);
    CHECK(test_if == 0);
    CHECK(link_host_close(0, 1) == 0);
    receive(LINK_OP_ACK, LINK_F_RELACK, 0, 0, link_h[0].rel_rid, 0, 0);
    link_tick();
    CHECK(link_host_open("TIME", 4, 1) == 0);
    CHECK(link_host_status(0, 0, 0, 1) == OS32_ERR_AGAIN);
    int rc = verify_recovery();
    return rc ? rc : verify_rx();
}
/* Real tick cadence, including wrap, NIC backpressure, close and owner exit. */
static int verify_recovery(void)
{
    const u8 mac[6] = {2, 3, 4, 5, 6, 7};
    for (int mode = 0; mode < 4; mode++) {
        link_init(mac);
        link_state = LINK_S_UP; link_hs_due = 0;
        link_epoch = 3; link_sess = 9; link_hello_ok = 1;
        link_resyncs = 0; tx_result = 0;
        tick_count = U32_MAX - 49;
        u32 start = tick_count;
        i32 h = link_host_open("ECHO 4", 6, 1);
        CHECK(h == 0);
        link_tick();
        if (mode) {
            receive(LINK_OP_ACK, 0, 0, 0, link_h[h].rid, 0, 0);
            link_tick();
        }
        if (mode == 1) {
            CHECK(link_host_write(h, "abcd", 4, 1) == 4);
            link_tick();
        }
        if (mode == 3) {
            u8 pl[LINK_RESP_LEN];
            le_write16(pl, 200); le_write32(pl + 2, 4);
            receive(LINK_OP_RESPONSE, 0, 0, 0, link_h[h].rid, pl, sizeof(pl));
            link_tick();
        }
        u32 interval = mode < 2 ? LINK_RTO_TICKS : LINK_PROBE_TICKS;
        u32 end = interval * (mode < 2 ? LINK_TRIES + 1 : LINK_PROBE_MAX + 1);
        for (u32 elapsed = 1; elapsed <= end; elapsed++) {
            tick_count = start + elapsed;
            link_tick();
            if (elapsed == LINK_HOST_SILENCE_TICKS) {
                CHECK(link_host_status(h, 0, 0, 1) == OS32_ERR_HOSTDOWN);
                CHECK(link_resyncs == 0);
                CHECK(!link_h[h].req_due && !link_h[h].wlen);
                if (mode & 1) link_host_owner_exit(1);
                else CHECK(link_host_close(h, 1) == 0);
                /* Pending cleanup belongs to the old rid until acknowledged. */
                CHECK(link_host_open("TIME", 4, 2) == 1);
                receive(LINK_OP_ACK, 0, 0, 0, link_h[1].rid, 0, 0);
            }
            if (elapsed > LINK_HOST_SILENCE_TICKS && elapsed < end) {
                /* Keep the other request alive: only the old failure budget
                 * may trigger recovery at the original RTO/PROBE boundary. */
                link_host_progress(1);
                link_h[1].last_rx_tick = tick_count;
            }
            CHECK(link_resyncs == (elapsed == end ? 1u : 0u));
        }
        CHECK(link_state == LINK_S_SYN);
    }
    /* A pending cleanup transmission has no RTO until NIC acceptance. */
    link_init(mac); link_state = LINK_S_UP; link_hs_due = 0;
    link_epoch = 3; link_sess = 9; link_hello_ok = 1; link_resyncs = 0;
    u32 start = tick_count;
    CHECK(link_host_open("TIME", 4, 1) == 0);
    link_tick();
    for (u32 elapsed = 1; elapsed <= 150; elapsed++) {
        tick_count = start + elapsed;
        if (elapsed == LINK_HOST_SILENCE_TICKS) tx_result = OS32_ERR_AGAIN;
        link_tick();
        CHECK(link_resyncs == 0);
    }
    CHECK(link_h[0].rel_pending && link_h[0].rel_due);
    CHECK(link_h[0].rel_tries == LINK_TRIES);
    tx_result = 0; tick_count++;
    link_tick();
    CHECK(!link_h[0].rel_due && link_resyncs == 0);
    tick_count += LINK_RTO_TICKS - 1;
    link_tick(); CHECK(link_resyncs == 0);
    tick_count++;
    link_tick(); CHECK(link_resyncs == 1);
    /* A healthy peer's RELACK stops cleanup without changing the session. */
    link_init(mac); link_state = LINK_S_UP; link_hs_due = 0;
    link_epoch = 7; link_sess = 4; link_hello_ok = 1; link_resyncs = 0;
    i32 h = link_host_open("TIME", 4, 1);
    link_tick();
    receive(LINK_OP_ACK, 0, 0, 0, link_h[h].rid, 0, 0);
    link_tick();
    tick_count += LINK_HOST_SILENCE_TICKS;
    link_tick();
    CHECK(link_host_status(h, 0, 0, 1) == OS32_ERR_HOSTDOWN);
    receive(LINK_OP_ACK, LINK_F_RELACK, 0, 0, link_h[h].rid, 0, 0);
    link_tick();
    tick_count += LINK_PROBE_TICKS * (LINK_PROBE_MAX + 1);
    link_tick();
    CHECK(link_resyncs == 0 && link_sess == 4 && link_epoch == 7);
    CHECK(link_host_status(h, 0, 0, 1) == OS32_ERR_HOSTDOWN);
    return 0;
}
/* Frames enter through the shared RX boundary, not direct dispatch calls. */
static int verify_rx(void)
{
    const u8 mac[6] = {2, 3, 4, 5, 6, 7};
    tx_result = 0;
    u8 pl[LINK_NOTIFY_MAX_LEN] = {0};
    const u8 *out;
    for (int ext = 0; ext <= 1; ext++) {
        link_init(mac);
        tick_count += 1;
        link_epoch = 7; link_sess = 4;
        le_write16(pl, 9);
        le_write16(pl + LINK_HELLO_REQ_SESS_OFF, link_syn_sess);
        le_write16(pl + LINK_HELLO_REQ_EPOCH_OFF, link_syn_epoch);
        le_write32(pl + LINK_HELLO_SA_HOST_GEN_OFF, 1234);
        le_write16(pl + LINK_HELLO_SA_ALIVE_OFF, 23);
        receive(LINK_OP_HELLO, LINK_HS_SYNACK, link_nonce, 456, 0, pl,
                ext ? LINK_HELLO_SA_EXT_LEN : LINK_HELLO_SA_LEN);
        link_rx_pump();
        CHECK(link_state == LINK_S_CONFIRM);
        le_write32(pl + LINK_HELLO_ES_HOST_GEN_OFF, 1234);
        le_write16(pl + LINK_HELLO_ES_ALIVE_OFF, 23);
        receive(LINK_OP_HELLO, LINK_HS_ESTAB, 456, link_nonce, 0, pl,
                ext ? LINK_HELLO_ES_EXT_LEN : LINK_HELLO_ES_LEN);
        link_rx_pump();
        CHECK(link_state == LINK_S_UP && active && extended_hello == ext);
        CHECK(host_generation == (ext ? 1234u : 0u));
        CHECK(alive_service == (ext ? 23 : 0));
        /* Only the two exact response lengths are legal. */
        for (int n = 0; n <= 14; n++) {
            CHECK(link_paylen_ok(LINK_OP_HELLO, LINK_HS_SYNACK, n) ==
                  (n == LINK_HELLO_SA_LEN || n == LINK_HELLO_SA_EXT_LEN));
            CHECK(link_paylen_ok(LINK_OP_HELLO, LINK_HS_ESTAB, n) ==
                  (n == LINK_HELLO_ES_LEN || n == LINK_HELLO_ES_EXT_LEN));
        }
        CHECK(link_paylen_ok(LINK_OP_HELLO, LINK_HS_SYN, 0));
        CHECK(!link_paylen_ok(LINK_OP_HELLO, LINK_HS_CONFIRM, 1));
        kmemset(pl, 0, sizeof(pl));
        le_write32(pl, 1234); le_write16(pl + LINK_NOTIFY_SERVICE_OFF, 23);
        le_write16(pl + LINK_NOTIFY_KIND_OFF, LINK_NOTIFY_ALIVE_KIND);
        le_write16(pl + LINK_NOTIFY_LENGTH_OFF, 4);
        le_write32(pl + LINK_NOTIFY_DATA_OFF, 1234);
        int before = mailbox_calls;
        receive(LINK_OP_NOTIFY, 0, 1, 0, 0, pl, 16);
        /* Wrong destination, peer, session, epoch, rid and size are rejected. */
        for (int bad = 0; bad < 7; bad++) {
            receive(LINK_OP_NOTIFY, 0, 1, 0, 0, pl, 16);
            u8 *h = rx_frame + ETH_HLEN;
            if (bad == 0) kmemcpy(rx_frame, link_bcast, ETH_ALEN);
            if (bad == 1) rx_frame[ETH_ALEN] ^= 1;
            if (bad == 2) h[LINK_OFF_SESS] ^= 1;
            if (bad == 3) h[LINK_OFF_EPOCH] ^= 1;
            if (bad == 4) h[LINK_OFF_RID] = 1;
            if (bad == 5) le_write16(h + LINK_OFF_LENGTH, LINK_NOTIFY_DATA_OFF - 1);
            if (bad == 6) le_write16(h + LINK_OFF_LENGTH, LINK_NOTIFY_MAX_LEN + 1);
            link_rx_pump();
            CHECK(mailbox_calls == before);
        }
        receive(LINK_OP_NOTIFY, 0, 1, 0, 0, pl, 16);
        link_tick();
        CHECK(mailbox_calls == before + 1 && mailbox_alive == ext && ack_seq == 1);
        CHECK(!want_ack && !want_window);
        CHECK(le_read32(tx_frame + ETH_HLEN + LINK_OFF_RID) == 0);
        CHECK(le_read16(tx_frame + ETH_HLEN + LINK_HDR_LEN) == 4);
        receive(LINK_OP_NOTIFY, 0, 1, 0, 0, pl, 16);
        link_rx_pump(); CHECK(mailbox_calls == before + 1);
        link_start_hello(0, 1); CHECK(!active);
    }
    link_state = LINK_S_UP; link_hs_due = 0; link_hello_ok = 1;
    i32 h = link_host_open("ECHO 4", 6, 1);
    CHECK(h >= 0);
    u32 rid = link_h[h].rid;
    tick_count += 90;
    receive(LINK_OP_ACK, 0, 0, 0, rid, 0, 0);
    link_tick(); CHECK(link_deadline[h].progress_tick == tick_count);
    tick_count += 90;
    CHECK(link_host_write(h, "abcd", 4, 1) == 4);
    receive(LINK_OP_ACK, 0, 0, link_h[h].wseq, rid, 0, 0);
    link_tick(); CHECK(link_deadline[h].progress_tick == tick_count);
    tick_count += 1;
    receive(LINK_OP_ACK, 0, 0, 0, rid, 0, 0);
    link_tick(); CHECK(link_deadline[h].progress_tick == tick_count - 1);
    for (int i = 0; i < 4; i++) {
        tick_count += 80;
        le_write16(pl, LINK_CTL_PROCESSING); le_write32(pl + 2, 0);
        receive(LINK_OP_RESPONSE, LINK_F_CTRL, 0, 0, rid, pl, LINK_RESP_LEN);
        link_tick(); CHECK(link_host_status(h, 0, 0, 1) == OS32_ERR_AGAIN);
        CHECK(link_deadline[h].progress_tick == tick_count);
    }
    le_write16(pl, 200); le_write32(pl + 2, 1000);
    receive(LINK_OP_RESPONSE, 0, 0, 0, rid, pl, LINK_RESP_LEN);
    link_tick(); CHECK(link_host_status(h, 0, 0, 1) == 0);
    CHECK(link_host_read_stage(h, 8, &out, 1) == OS32_ERR_AGAIN);
    for (int i = 1; i <= 4; i++) {
        tick_count += 90;
        receive(LINK_OP_DATA, 0, i, 0, rid, pl, 8);
        link_tick(); CHECK(link_deadline[h].progress_tick == tick_count);
        CHECK(link_host_read_stage(h, 8, &out, 1) == 8);
    }
    u32 progress = link_deadline[h].progress_tick;
    for (int i = 0; i < 3; i++) {
        tick_count += 20;
        receive(LINK_OP_DATA, 0, i == 0 ? 4 : 6, 0, rid, pl, 8);
        link_tick(); CHECK(link_deadline[h].progress_tick == progress);
    }
    tick_count = progress + 100;
    link_tick(); CHECK(link_host_status(h, 0, 0, 1) == OS32_ERR_HOSTDOWN);
    CHECK(rx_calls > 0 && test_if == 0);
    return 0;
}
void _start(void)
{
    int rc = verify();
    if (rc) {
        char message[] = "FAIL line 0000\n";
        int line = rc;
        for (int i = 13; i >= 10; i--) { message[i] = '0' + line % 10; line /= 10; }
        __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(message),
                         "d"(sizeof(message) - 1) : "memory");
        rc = 1;
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
