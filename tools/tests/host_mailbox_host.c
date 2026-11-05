/* NOTICE unit: real mailbox, fixture-owned IF/heap/copyout seams; no libc. */
#include "hostsvc_fixture.h"
#include "lib/kstring.h"
#include "os32_kapi_shared.h"
#define IO_H
static unsigned int irq_save(void)
{
    unsigned int flags = hs_if();
    hs_set_if(0);
    return flags;
}
static void irq_restore(unsigned int flags) { hs_set_if(flags); }
static unsigned char heap[16384]; /* §5: exactly 64 x 256 bytes */
static int allocations;
void *kmalloc(u32 size)
{
    int rc;
    allocations++;
    if (hs_failure_take("heap", &rc)) return 0;
    if (size != sizeof(heap) || allocations != 1) __builtin_trap();
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
#include PRODUCT_SOURCE
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
static int put(u32 gen, u16 service, u32 value, int alive)
{
    return host_mailbox_put(gen, service, 1, &value, sizeof(value), alive);
}
static void overflow_hook(void)
{
    if (put(7, 2, 999, 0)) __builtin_trap();
}
static int copyout(HostNotice *out, const HostNotice *in)
{
    int rc;
    hs_copyout_run_hook();
    if (hs_failure_take("copyout", &rc)) return rc;
    *out = *in;
    return 0;
}
static int suite(int fail_heap)
{
    HostNotice n, again, user;
    struct HostAlive a;
    hs_reset(0xFFFFFF00u);
    CHECK(host_mailbox_latest_serial() == 0 && hs_if());
    host_alive_snapshot(&a);
    CHECK(!a.valid && a.observation == 0 && hs_if());
    CHECK(host_mailbox_stage(0, 65535, &n) == OS32_ERR_NOSYS);
    if (fail_heap) hs_fail("heap", -1);
    CHECK(host_mailbox_init() == (fail_heap ? OS32_ERR_NOSYS : 0));
    CHECK(host_mailbox_init() == (fail_heap ? OS32_ERR_NOSYS : 0));
    CHECK(allocations == 1);
    CHECK(host_mailbox_latest_serial() == 0 && hs_if());
    if (fail_heap) {
        hs_set_if(0);
        CHECK(put(7, 3, 7, 1) == OS32_ERR_NOSYS);
        CHECK(host_mailbox_latest_serial() == 0 && !hs_if());
        host_alive_snapshot(&a);
        CHECK(a.valid && a.host_generation == 7 && a.observation == 1 && !hs_if());
        CHECK(host_mailbox_stage(7, 3, &n) == OS32_ERR_NOSYS && !hs_if());
        host_mailbox_commit(7, 1);
        CHECK(!hs_if());
        return 0;
    }
    CHECK(host_mailbox_stage(0, 1, &n) == OS32_ERR_INVAL);
    CHECK(host_mailbox_stage(7, 0, &n) == OS32_ERR_INVAL);
    CHECK(host_mailbox_stage(7, 65536, &n) == OS32_ERR_INVAL);
    CHECK(host_mailbox_stage(7, 2, 0) == OS32_ERR_INVAL);
    CHECK(host_mailbox_put(7, 0, 1, 0, 0, 0) == OS32_ERR_INVAL);
    CHECK(host_mailbox_put(7, 65535, 1, 0, 0, 0) == OS32_ERR_INVAL);
    CHECK(host_mailbox_put(7, 2, 1, 0, 1, 0) == OS32_ERR_INVAL);
    CHECK(host_mailbox_put(7, 2, 1, heap, 237, 0) == OS32_ERR_INVAL);
    CHECK(host_mailbox_stage(0, 65535, &n) == OS32_ERR_AGAIN);
    CHECK(host_mailbox_latest_serial() == 0);
    for (u32 i = 1; i <= 65; i++) CHECK(put(7, i == 3 ? 3 : 2, i, 0) == 0);
    CHECK(host_mailbox_latest_serial() == 65 && hs_if());
    CHECK(host_mailbox_latest_serial() == 65 && hs_if());
    CHECK(host_mailbox_stage(7, 2, &n) == 0);
    CHECK(n.serial == 2 && n.dropped == 1 && n.data[0] == 2);
    CHECK(n.length == 4 && n.reserved == 0 && n.data[235] == 0);
    CHECK(host_mailbox_stage(7, 3, &again) == 0 && again.serial == 3);
    host_mailbox_commit(8, again.serial);
    CHECK(host_mailbox_stage(7, 3, &again) == 0);
    host_mailbox_commit(7, again.serial);
    CHECK(host_mailbox_latest_serial() == 65);
    CHECK(host_mailbox_stage(7, 3, &again) == OS32_ERR_AGAIN);
    CHECK(host_mailbox_stage(7, 2, &again) == 0 && again.serial == n.serial);
    hs_fail("copyout", -77);
    CHECK(copyout(&user, &n) == -77);
    CHECK(host_mailbox_stage(7, 2, &again) == 0 && again.serial == n.serial);
    /* Refill to 64, then copyout hook evicts the staged oldest entry. */
    CHECK(put(7, 2, 66, 0) == 0);
    hs_copyout_hook(overflow_hook);
    CHECK(copyout(&user, &n) == 0);
    host_mailbox_commit(n.host_generation, n.serial);
    CHECK(host_mailbox_stage(7, 2, &again) == 0 && again.serial == 4);
    int drained = 0;
    while (host_mailbox_stage(0, 65535, &n) == 0) {
        host_mailbox_commit(n.host_generation, n.serial);
        drained++;
    }
    CHECK(drained == 64);
    CHECK(host_mailbox_latest_serial() == 67);
    /* ALIVE refresh must not evict any of the 63 ordinary notifications. */
    CHECK(put(7, 3, 7, 1) == 0);
    for (u32 i = 0; i < 63; i++) CHECK(put(7, 2, i, 0) == 0);
    for (u32 i = 0; i < 100; i++) CHECK(put(7, 3, 7, 1) == 0);
    host_alive_snapshot(&a);
    CHECK(a.valid && a.host_generation == 7 && a.observation == 101);
    CHECK(host_mailbox_stage(7, 3, &n) == 0);
    CHECK(n.dropped == 2);
    /* A refreshed ALIVE's token cannot be consumed by an older commit. */
    CHECK(put(7, 3, 7, 1) == 0);
    host_mailbox_commit(7, n.serial);
    CHECK(host_mailbox_stage(7, 3, &again) == 0 && again.serial != n.serial);
    host_mailbox_commit(7, again.serial);
    CHECK(host_mailbox_latest_serial() == again.serial);
    CHECK(host_mailbox_stage(7, 3, &n) == OS32_ERR_AGAIN);
    host_alive_snapshot(&a);
    CHECK(a.valid && a.observation == 102);
    drained = 0;
    while (host_mailbox_stage(7, 2, &n) == 0) {
        host_mailbox_commit(7, n.serial);
        drained++;
    }
    CHECK(drained == 63);
    CHECK(host_mailbox_stage(0, 65535, &n) == OS32_ERR_AGAIN);
    /* Consumed ALIVE still permits latest-value refresh without a new ring slot. */
    CHECK(put(7, 3, 7, 1) == 0);
    CHECK(host_mailbox_stage(7, 3, &n) == 0);
    CHECK(put(8, 4, 8, 1) == 0);
    CHECK(host_mailbox_stage(7, 3, &again) == OS32_ERR_STALE);
    CHECK(host_mailbox_stage(0, 65535, &again) == 0);
    CHECK(again.host_generation == 8 && again.service == 4 && again.dropped == 3);
    host_mailbox_commit(7, n.serial);
    host_mailbox_commit(8, again.serial);
    CHECK(host_mailbox_latest_serial() == again.serial);
    CHECK(host_mailbox_stage(8, 65535, &n) == OS32_ERR_AGAIN);
    host_alive_snapshot(&a);
    CHECK(a.valid && a.host_generation == 8 && a.observation == 104);
    hs_set_if(0);
    CHECK(put(8, 4, 8, 1) == 0 && !hs_if());
    CHECK(host_mailbox_stage(8, 4, &n) == 0 && !hs_if());
    host_mailbox_commit(8, n.serial);
    CHECK(host_mailbox_latest_serial() == n.serial && !hs_if());
    host_alive_snapshot(&a);
    CHECK(!hs_if());
    unsigned char payload[236];
    for (u32 i = 0; i < sizeof(payload); i++) payload[i] = (u8)i;
    CHECK(host_mailbox_put(8, 2, 9, payload, sizeof(payload), 0) == 0);
    payload[235] = 0;
    CHECK(host_mailbox_stage(8, 2, &n) == 0 && n.length == 236);
    CHECK(n.data[235] == 235 && n.kind == 9 && !hs_if());
    CHECK(host_mailbox_put(8, 5, 2, 0, 0, 0) == 0);
    CHECK(put(9, 2, 99, 0) == 0);
    host_mailbox_commit(8, n.serial);
    CHECK(host_mailbox_stage(9, 2, &n) == 0 && n.dropped == 5);
    CHECK(n.host_generation == 9 && n.data[0] == 99 && !hs_if());
    CHECK(host_mailbox_stage(9, 5, &again) == OS32_ERR_AGAIN);
    host_alive_snapshot(&a);
    CHECK(a.valid && a.host_generation == 8 && a.observation == 105);
    hs_advance(256);
    CHECK(hs_now() == 0);
    return 0;
}
void entry(unsigned int *stack)
{
    int rc = suite(stack[0] > 1);
    /* Keep runtime failures distinct from compile/runner failures. */
    if (rc) rc = 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
__asm__(".global _start\n_start:\npush %esp\ncall entry\n");
