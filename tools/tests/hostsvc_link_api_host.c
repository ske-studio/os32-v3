/* Real link.c/A11 under ILP32, no libc. Only IRQ/clock/NIC are seams. */
#define LINK_HOST_TEST
#include "types.h"
volatile u32 tick_count;
static unsigned int test_if = 1;
unsigned int link_test_irq_save(void) { unsigned int f = test_if; test_if = 0; return f; }
void link_test_irq_restore(unsigned int f) { test_if = f; }
void link_test_idle(void) { }
#include "net/link.c"
int net_mac(u8 out[6]) { (void)out; return 0; }
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
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int verify(void)
{
    link_ready = 1;
    link_state = LINK_S_DOWN;
    CHECK(link_host_open("TIME", 4, 1) == OS32_ERR_HOSTDOWN);
    link_state = LINK_S_UP; link_epoch = 7;
    tick_count = U32_MAX - 49;
    for (int i = 0; i < 8; i++) CHECK(link_host_open("TIME", 4, 1) == i);
    CHECK(link_host_open("TIME", 4, 1) == OS32_ERR_FULL);
    tick_count += 99;
    /* STATUS scheduler's timestamp is deliberately unrelated to progress. */
    link_h[0].last_rx_tick = tick_count;
    link_host_deadlines();
    CHECK(link_host_status(0, 0, 0, 1) == OS32_ERR_AGAIN);
    link_host_progress(1); /* PROCESSING or advancing body/ACK, wired by A8. */
    tick_count++;
    link_host_deadlines();
    CHECK(link_host_status(0, 0, 0, 1) == OS32_ERR_HOSTDOWN);
    CHECK(link_host_status(1, 0, 0, 1) == OS32_ERR_AGAIN);
    CHECK(link_epoch == 7 && link_state == LINK_S_UP && link_resyncs == 0);
    CHECK(!link_h[0].req_due && !link_h[0].w_due && !link_h[0].want_status);
    const u8 *out = 0;
    CHECK(link_host_read_stage(0, 1, &out, 1) == OS32_ERR_HOSTDOWN);
    CHECK(link_host_write(0, "x", 1, 1) == OS32_ERR_HOSTDOWN);
    CHECK(link_host_close(0, 1) == 0);
    CHECK(link_host_open("TIME", 4, 1) == 0);
    CHECK(link_host_status(0, 0, 0, 1) == OS32_ERR_AGAIN);
    link_h[0].state = LINK_H_RESP; link_h[0].length = link_h[0].recv_bytes = 4;
    tick_count += 100;
    link_host_deadlines();
    CHECK(link_host_status(0, 0, 0, 1) == 0); /* Local read is not peer silence. */
    CHECK(link_host_status(1, 0, 0, 1) == OS32_ERR_HOSTDOWN);
    link_epoch++;
    CHECK(link_host_status(1, 0, 0, 1) == OS32_ERR_STALE);
    CHECK(test_if == 1);
    return 0;
}
void _start(void)
{
    int rc = verify();
    if (rc) rc = 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
