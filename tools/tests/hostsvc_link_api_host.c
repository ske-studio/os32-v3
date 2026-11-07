/* Real link.c/A11 under ILP32, no libc. Only IRQ/clock/NIC are seams. */
#define LINK_HOST_TEST
#include "types.h"
volatile u32 tick_count;
static unsigned int test_if = 1;
unsigned int link_test_irq_save(void) { unsigned int f = test_if; test_if = 0; return f; }
void link_test_irq_restore(unsigned int f) { test_if = f; }
void link_test_idle(void) { }
#include "net/link.c"
/* API-only fixture: B2 notification behavior is covered by link-core. */
static int notify_resets;
void link_notify_reset(void) { notify_resets++; }
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
static int verify_named_body(void)
{
    static const char *const requests[] = {
        "os32.clip PUT 3", "os32.print DATA 1 3",
        "CLIP PUT 3", "PRINT DATA 1 3", "ECHO 3", "PUT /file/x 3"
    };
    static const u32 lengths[] = {sizeof("os32.clip PUT 3") - 1, sizeof("os32.print DATA 1 3") - 1,
        sizeof("CLIP PUT 3") - 1, sizeof("PRINT DATA 1 3") - 1,
        sizeof("ECHO 3") - 1, sizeof("PUT /file/x 3") - 1};
    link_ready = 1; link_state = LINK_S_UP; link_epoch = 7;
    for (u32 i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
        i32 h = link_host_open(requests[i], lengths[i], 1);
        CHECK(h == 0);
        CHECK(link_h[h].decl_len == 3);
        CHECK(link_host_write(h, "abc", 3, 1) == OS32_ERR_AGAIN);
        link_h[h].req_acked = 1;
        CHECK(link_host_write(h, "abcd", 4, 1) == OS32_ERR_INVAL);
        CHECK(link_host_write(h, "abc", 3, 1) == 3);
        CHECK(link_h[h].wlen == 3 && link_h[h].wsent_total == 3);
        CHECK(link_h[h].wcopy[0] == 'a' && link_h[h].wcopy[2] == 'c');
        CHECK(link_host_close(h, 1) == 0);
        link_h[h].rel_pending = 0;
    }
    CHECK(link_decl_len("os32.clip GET", 13) == 0);
    CHECK(link_decl_len("os32.print STATUS 1", 19) == 0);
    CHECK(link_decl_len("os32.clip PUT 65537", sizeof("os32.clip PUT 65537") - 1) == 0);
    return 0;
}
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
    /* RID exhaustion reaches the A9 reset seam through link_resync. */
    link_next_rid = U32_MAX;
    CHECK(link_host_open("TIME", 4, 1) == OS32_ERR_HOSTDOWN);
    CHECK(notify_resets == 1 && link_resyncs == 1 && link_state == LINK_S_SYN);
    CHECK(test_if == 1);
    return 0;
}
void _start(void)
{
    int rc = verify_named_body();
    if (!rc) rc = verify();
    if (rc) rc = 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
