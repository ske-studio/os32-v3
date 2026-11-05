/* B4 with the real B3 mailbox and fixture-controlled caller copy. */
#include "hostsvc_fixture.h"
#include "lib/kstring.h"
#include "redir_access.h"
#include "net/host_mailbox.h"
#include "os32_kapi_shared.h"
#define IO_H
static unsigned int irq_save(void) { unsigned int f = hs_if(); hs_set_if(0); return f; }
static void irq_restore(unsigned int f) { hs_set_if(f); }
static unsigned char heap[64 * sizeof(HostNotice)];
void *kmalloc(u32 n) { return n == sizeof(heap) ? heap : 0; }
void *kmemset(void *d, int v, u32 n) { u8 *p = d; while (n--) *p++ = v; return d; }
void *kmemcpy(void *d, const void *s, u32 n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
#include "net/host_mailbox.c"
static u32 capacity;
static int identity = 1;
int caller_access_get(struct caller_access *c) { (void)c; return identity; }
int check_caller_write_range(const struct caller_access *c, void *p, u32 n)
{ (void)c; return p && n <= capacity; }
int copy_to_caller(const struct caller_access *c, void *d, const void *s, u32 n)
{
    int rc;
    (void)c;
    hs_copyout_run_hook();
    if (hs_failure_take("copyout", &rc)) return 0;
    kmemcpy(d, s, n);
    return 1;
}
#include PRODUCT_SOURCE
#define CHECK(c) do { if (!(c)) return 1; } while (0)
static void overflow(void)
{ if (host_mailbox_put(7, 2, 1, 0, 0, 0)) __builtin_trap(); }
static int suite(void)
{
    HostNotice out;
    hs_reset(0);
    capacity = sizeof(out);
    CHECK(host_mailbox_init() == 0);
    CHECK(kapi_host_notice_serial() == 0);
    CHECK(host_mailbox_put(7, 2, 1, 0, 0, 0) == 0);
    CHECK(kapi_host_notice_serial() == 1 && kapi_host_notice_serial() == 1);
    CHECK(kapi_host_notice_read(7, 2, 0) == OS32_ERR_INVAL);
    capacity--;
    CHECK(kapi_host_notice_read(7, 2, &out) == OS32_ERR_INVAL);
    capacity++;
    identity = 0;
    CHECK(kapi_host_notice_read(7, 2, &out) == OS32_ERR_INVAL);
    identity = 1;
    CHECK(kapi_host_notice_read(0, 2, &out) == OS32_ERR_INVAL);
    CHECK(kapi_host_notice_read(7, 0, &out) == OS32_ERR_INVAL);
    CHECK(kapi_host_notice_read(8, 2, &out) == OS32_ERR_STALE);
    hs_fail("copyout", -1);
    CHECK(kapi_host_notice_read(7, 2, &out) == OS32_ERR_INVAL);
    CHECK(kapi_host_notice_read(0, 65535, &out) == 0 && out.serial == 1);
    CHECK(kapi_host_notice_serial() == 1);
    CHECK(kapi_host_notice_read(7, 2, &out) == OS32_ERR_AGAIN);
    for (int i = 0; i < 64; i++) CHECK(host_mailbox_put(7, 2, 1, 0, 0, 0) == 0);
    hs_copyout_hook(overflow);
    CHECK(kapi_host_notice_read(7, 2, &out) == 0 && out.serial == 2);
    CHECK(kapi_host_notice_read(7, 2, &out) == 0 && out.serial == 3);
    CHECK(kapi_host_notice_serial() == 66 && hs_if());
    return 0;
}
void entry(void) { int rc = suite(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
__asm__(".global _start\n_start:\ncall entry\n");
