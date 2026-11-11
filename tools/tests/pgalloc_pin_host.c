#include "types.h"
static unsigned int irq_save(void) { return 0; }
static void irq_restore(unsigned int f) { (void)f; }
static void _stop(void) { for (;;) {} }
#define IO_H
#include "pgalloc.c"
#include "pgalloc_host_fixture.h"
int paging_boot_context(void) { return 1; }
void kprintf(unsigned char a, const char *f, ...) { (void)a; (void)f; }
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int test(void)
{
    u32 owner, quar, p, before, out = 99, bad, i;
    for (i = 0; i < sizeof(host_pool_storage) / sizeof(host_pool_storage[0]); i++)
        host_pool_storage[i] = 0xa5a5a5a5;
    CHECK(!pgalloc_pinned(0) && !pgalloc_pinned(PHYSMEM_MAX_PFN - 1));
    host_pool_boot(16384);
    CHECK(pin_map == owner_map + PGALLOC_OWNER_BYTES(limit_pfn));
    CHECK(pin_map + PGALLOC_PIN_BYTES(limit_pfn) ==
          (u8 *)host_pool_storage + PGALLOC_META_BYTES(limit_pfn));
    for (i = 0; i < limit_pfn; i++) CHECK(!pgalloc_pinned(i));
    CHECK(!pgalloc_pinned(limit_pfn));
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 1, "as", &owner));
    CHECK(ledger_owner_new(LEDGER_KIND_DEVICE, 2, "quar", &quar));
    CHECK(pgalloc_alloc_n_owner(owner, 3, MEM_POOL_BASE / PAGE_SIZE, limit_pfn, 0, &p));
    before = pgalloc_free_pages();
    CHECK(pgalloc_pin(p + 1, 1, owner));
    bad = ledger_bad_free;
    CHECK(!pgalloc_free_n_owner(owner, p, 3));
    CHECK(ledger_bad_free == ++bad && pgalloc_free_pages() == before && ledger_owner_pages(owner) == 3);
    CHECK(!ledger_transfer(p, 3, owner, LEDGER_OWNER_KERNEL));
    CHECK(ledger_bad_free == ++bad && ledger_owner_pages(owner) == 3);
    CHECK(!ledger_reclaim_owner(owner, &out));
    CHECK(ledger_bad_free == ++bad && out == 99 && ledger_owner_pages(owner) == 3);
    CHECK(ledger_transfer_pinned(p + 1, 1, owner, quar));
    CHECK(pgalloc_pinned(p + 1) == 1 && ledger_owner_pages(quar) == 1);
    CHECK(!pgalloc_unpin(p, 2, owner) && pgalloc_pinned(p + 1) == 1);
    CHECK(pgalloc_unpin(p + 1, 1, quar));
    CHECK(pgalloc_free_n_owner(quar, p + 1, 1));
    CHECK(ledger_reclaim_owner(owner, &out) && out == 2);
    CHECK(pgalloc_alloc_n_owner(owner, 2, p, p + 2, 0, &p));
    for (i = 0; i < 255; i++) CHECK(pgalloc_pin(p + 1, 1, owner));
    CHECK(!pgalloc_pin(p, 2, owner));
    CHECK(pgalloc_pinned(p) == 0 && pgalloc_pinned(p + 1) == 255);
    CHECK(!pgalloc_pin(p, 0, owner) && !pgalloc_pin(p, 0xffffffffU, owner));
    for (i = 0; i < 255; i++) CHECK(pgalloc_unpin(p + 1, 1, owner));
    CHECK(pgalloc_free_n_owner(owner, p, 2));
    return 0;
}
void _start(void) { int r = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(r)); for (;;) {} }
