/* NOTICE unit, libc-free ILP32. Real B2 + B3, shared IF/clock/failure seams. */
#include "hostsvc_fixture.h"
#include "lib/kstring.h"
#include "lib/le_internal.h"
#include "net/link.h"
#include "net/host_mailbox.h"
#include "os32_kapi_shared.h"
#define IO_H
static unsigned int irq_save(void)
{
    unsigned int flags = hs_if();
    hs_set_if(0);
    return flags;
}
static void irq_restore(unsigned int flags) { hs_set_if(flags); }
static u8 heap[64 * sizeof(HostNotice)];
static int allocations;
void *kmalloc(u32 size)
{
    int rc;
    allocations++;
    if (hs_failure_take("heap", &rc)) return 0;
    if (allocations != 1 || size != sizeof(heap)) __builtin_trap();
    return heap;
}
void *kmemset(void *dst, int value, u32 n)
{
    u8 *p = dst;
    for (u32 i = 0; i < n; i++) p[i] = value;
    return dst;
}
void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *p = dst;
    const u8 *q = src;
    for (u32 i = 0; i < n; i++) p[i] = q[i];
    return dst;
}
#include "net/host_mailbox.c"
#include PRODUCT_SOURCE
/* One tick may issue at most ACK + WINDOW. Records describe successful TX;
 * injected nic.tx errors are returned unchanged and consume no record. */
static struct { u8 op; u32 ack; u16 window; } controls[2];
static unsigned int controls_count, attempts;
static int fail_window_after_ack;
int link_notify_control(u8 op, u32 ack, u16 window)
{
    int rc;
    attempts++;
    if (hs_failure_take("nic.tx", &rc)) return rc;
    if (controls_count == 2) __builtin_trap();
    controls[controls_count].op = op;
    controls[controls_count].ack = ack;
    controls[controls_count++].window = window;
    if (op == LINK_OP_ACK && fail_window_after_ack) {
        fail_window_after_ack = 0;
        hs_fail("nic.tx", OS32_ERR_IO);
    }
    return 0;
}
#define CHECK(c) do { __asm__ volatile("" ::: "memory"); \
    if (!(c)) return __LINE__; } while (0)
static void tick(void)
{
    attempts = controls_count = 0;
    link_notify_tick(hs_now());
}
static int control(unsigned int pos, u8 op, u32 ack)
{
    return pos < controls_count && controls[pos].op == op &&
           controls[pos].ack == ack && controls[pos].window == 4;
}
static u8 payload[LINK_NOTIFY_MAX_LEN + 1];
/* Reference wire layout: NET_HOSTSVC_DESIGN §5; field offsets in link.h. */
static void wire(u32 gen, u16 service, u16 kind, u16 len)
{
    kmemset(payload, 0, sizeof(payload));
    le_write32(payload, gen);
    le_write16(payload + 4, service);
    le_write16(payload + 6, kind);
    le_write16(payload + 8, len);
    for (u16 i = 0; i < len; i++) payload[12 + i] = (u8)i;
    if (service == 23 && kind == 1 && len == 4)
        le_write32(payload + 12, gen);
}
static void receive(u32 seq, u32 gen, u16 service, u16 kind, u16 len)
{
    wire(gen, service, kind, len);
    link_notify_rx(seq, payload, 12 + len);
}
static void drain(void)
{
    HostNotice n;
    while (!host_mailbox_stage(0, 65535, &n))
        host_mailbox_commit(n.host_generation, n.serial);
}
static void race_hook(void) { receive(3, 7, 23, 1, 4); }
static int suite(int fail_heap)
{
    HostNotice n, newer;
    struct HostAlive a;
    hs_reset(0xFFFFFF00u);
    link_notify_reset();
    receive(1, 7, 23, 1, 4);
    tick();
    host_alive_snapshot(&a);
    CHECK(!a.valid && !attempts && !host_mailbox_latest_serial());
    if (fail_heap) hs_fail("heap", OS32_ERR_NOSYS);
    CHECK(host_mailbox_init() == (fail_heap ? OS32_ERR_NOSYS : 0));
    link_notify_session(9, 8, 1, 7, 23);
    hs_fail("nic.tx", OS32_ERR_AGAIN);
    tick();
    CHECK(attempts == 1 && controls_count == 0);
    hs_advance(255);
    tick();
    CHECK(attempts == 1 && control(0, LINK_OP_WINDOW, 0));
    hs_advance(1);
    CHECK(hs_now() == 0);
    tick();
    CHECK(!attempts);
    receive(1, 7, 23, 1, 4);
    host_alive_snapshot(&a);
    CHECK(a.valid && a.host_generation == 7 && a.observation == 1);
    hs_fail("nic.tx", OS32_ERR_IO);
    tick();
    CHECK(attempts == 1 && !controls_count);
    hs_advance(1);
    tick();
    CHECK(attempts == 2 && control(0, LINK_OP_ACK, 1) &&
          control(1, LINK_OP_WINDOW, 1));
    u32 serial = host_mailbox_latest_serial();
    receive(1, 7, 23, 1, 4);
    tick();
    host_alive_snapshot(&a);
    CHECK(a.observation == 1 && host_mailbox_latest_serial() == serial);
    CHECK(attempts == 1 && control(0, LINK_OP_ACK, 1));
    if (fail_heap) {
        CHECK(serial == 0);
        CHECK(host_mailbox_stage(0, 65535, &n) == OS32_ERR_NOSYS);
        receive(2, 7, 42, 9, 236);
        tick();
        CHECK(control(0, LINK_OP_ACK, 2) && control(1, LINK_OP_WINDOW, 2));
        return 0;
    }
    CHECK(host_mailbox_stage(7, 23, &n) == 0 && n.length == 4);
    host_mailbox_commit(7, n.serial);
    host_alive_snapshot(&a);
    CHECK(a.valid && a.observation == 1);
    /* A stage token cannot consume an ALIVE refreshed by a receive hook. */
    receive(2, 7, 23, 1, 4);
    CHECK(host_mailbox_stage(7, 23, &n) == 0);
    hs_copyout_hook(race_hook);
    hs_copyout_run_hook();
    host_mailbox_commit(7, n.serial);
    CHECK(host_mailbox_stage(7, 23, &newer) == 0 && newer.serial != n.serial);
    tick();
    CHECK(control(0, LINK_OP_ACK, 3) && control(1, LINK_OP_WINDOW, 3));
    serial = host_mailbox_latest_serial();
    receive(5, 7, 42, 9, 1); /* gap: no storage, cumulatively ACK 3 */
    tick();
    CHECK(host_mailbox_latest_serial() == serial && attempts == 1 &&
          control(0, LINK_OP_ACK, 3));
    receive(4, 7, 42, 9, 236);
    payload[12 + 235] = 0; /* mailbox must have synchronously copied */
    receive(5, 7, 43, 9, 0);
    hs_fail("nic.tx", OS32_ERR_AGAIN);
    tick();
    CHECK(!controls_count && attempts == 1);
    receive(6, 7, 44, 9, 1); /* deferred ACK coalesces to newest contiguous seq */
    tick();
    CHECK(control(0, LINK_OP_ACK, 6) && control(1, LINK_OP_WINDOW, 6));
    CHECK(host_mailbox_stage(7, 42, &n) == 0 && n.length == 236 &&
          n.data[235] == 235 && n.kind == 9 && n.host_generation == 7);
    /* Invalid records never advance ACK or touch mailbox/ALIVE. */
    serial = host_mailbox_latest_serial();
    link_notify_rx(7, 0, 12);
    for (u16 len = 0; len < 12; len++) link_notify_rx(7, payload, len);
    wire(7, 42, 9, 236);
    link_notify_rx(7, payload, sizeof(payload));
    link_notify_rx(7, payload, 12); /* declared length mismatch */
    receive(7, 6, 42, 9, 0);
    receive(7, 0, 42, 9, 0);
    receive(7, 7, 0, 9, 0);
    receive(7, 7, 65535, 9, 0);
    wire(7, 42, 9, 0);
    payload[10] = 1;
    link_notify_rx(7, payload, 12);
    receive(7, 7, 23, 1, 3);
    wire(7, 23, 1, 4);
    payload[12] ^= 1;
    link_notify_rx(7, payload, 16);
    tick();
    CHECK(!attempts && host_mailbox_latest_serial() == serial);
    receive(7, 7, 42, 9, 0);
    /* WINDOW failure survives a successful ACK, without repeating that ACK. */
    fail_window_after_ack = 1;
    tick();
    CHECK(attempts == 2 && controls_count == 1 && control(0, LINK_OP_ACK, 7));
    tick();
    CHECK(attempts == 1 && control(0, LINK_OP_WINDOW, 7));
    drain();
    /* Exactly 65 distinct messages, duplicate retries never inflate drops. */
    link_notify_session(10, 8, 1, 8, 23);
    tick();
    CHECK(control(0, LINK_OP_WINDOW, 0));
    serial = host_mailbox_latest_serial();
    for (u32 seq = 1; seq <= 65; seq++) receive(seq, 8, 42, 9, 1);
    CHECK(host_mailbox_stage(8, 42, &n) == 0 && n.serial == serial + 2 &&
          n.dropped == 1);
    tick();
    CHECK(control(0, LINK_OP_ACK, 65));
    receive(66, 8, 23, 1, 4);
    CHECK(host_mailbox_stage(8, 42, &n) == 0 && n.dropped == 2);
    host_alive_snapshot(&a);
    u32 observation = a.observation;
    for (u32 seq = 67; seq <= 166; seq++) receive(seq, 8, 23, 1, 4);
    CHECK(host_mailbox_stage(8, 42, &newer) == 0 && newer.serial == n.serial &&
          newer.dropped == 2);
    host_alive_snapshot(&a);
    CHECK(a.observation == observation + 100 && a.host_generation == 8);
    tick();
    CHECK(control(0, LINK_OP_ACK, 166));
    receive(166, 8, 23, 1, 4);
    tick();
    CHECK(attempts == 1 && control(0, LINK_OP_ACK, 166));
    host_alive_snapshot(&a);
    CHECK(a.observation == observation + 100);
    /* Reconnection resets only wire sequencing; mailbox/snapshot survive. */
    serial = host_mailbox_latest_serial();
    link_notify_reset();
    receive(167, 8, 42, 9, 1);
    tick();
    CHECK(!attempts && host_mailbox_latest_serial() == serial);
    link_notify_session(11, 9, 1, 8, 23);
    CHECK(host_mailbox_stage(8, 42, &n) == 0 && n.serial == newer.serial);
    receive(1, 8, 42, 9, 1);
    tick();
    CHECK(control(0, LINK_OP_ACK, 1));
    /* New host generation is accepted only after session identity updates. */
    link_notify_session(12, 10, 1, 9, 24);
    tick();
    receive(1, 8, 23, 1, 4);
    tick();
    CHECK(!attempts);
    receive(1, 9, 24, 1, 4); /* explicit data generation for new alive id */
    wire(9, 24, 1, 4);
    le_write32(payload + 12, 9);
    link_notify_rx(1, payload, 16);
    host_alive_snapshot(&a);
    CHECK(a.host_generation == 9 && a.observation == observation + 101);
    CHECK(host_mailbox_stage(8, 42, &n) == OS32_ERR_STALE);
    tick();
    CHECK(control(0, LINK_OP_ACK, 1));
    /* Legacy HELLO extension bytes are invalid; internal ALIVE stays unchanged. */
    link_notify_session(13, 11, 0, 123, 23);
    receive(1, 10, 23, 1, 4);
    host_alive_snapshot(&a);
    CHECK(a.host_generation == 9 && a.observation == observation + 101);
    CHECK(host_mailbox_stage(10, 23, &n) == 0);
    tick();
    CHECK(control(0, LINK_OP_ACK, 1));
    receive(2, 9, 42, 9, 0);
    tick();
    CHECK(!attempts);
    /* Seed the sequence boundary without billions of fixture operations. */
    ack_seq = 0xFFFFFFFEu;
    receive(0xFFFFFFFFu, 10, 42, 9, 0);
    tick();
    CHECK(control(0, LINK_OP_ACK, 0xFFFFFFFFu));
    receive(0, 10, 42, 9, 0);
    tick();
    CHECK(control(0, LINK_OP_ACK, 0));
    serial = host_mailbox_latest_serial();
    receive(0xFFFFFFFFu, 10, 42, 9, 0);
    tick();
    CHECK(attempts == 1 && control(0, LINK_OP_ACK, 0) &&
          host_mailbox_latest_serial() == serial && hs_if());
    return 0;
}
void entry(unsigned int *stack)
{
    int line = suite(stack[0] > 1);
    if (line) {
        char message[] = "FAIL line 0000\n";
        for (int i = 13; i >= 10; i--) { message[i] = '0' + line % 10; line /= 10; }
        __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(message), "d"(15) : "memory");
        line = 1;
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(line));
    __builtin_unreachable();
}
__asm__(".global _start\n_start:\npush %esp\ncall entry\n");
