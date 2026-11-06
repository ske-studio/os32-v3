/* GF_GUARD_LIFETIME_DESIGN §2/A3. Real MM, no GPU or libc.
 * Synthetic BAR covers the final two PDEs, including the 4GiB end PFN.
 * Nouveau nouveau_bo.c:669-689 supplies the map/unmap lifetime model;
 * strict all-or-nothing PTE/ledger publication is the OS32 contract. */
#include "types.h"
static u32 host_cr3;
static void host_flush(void);
static void host_irq_boundary(void);
#include "paging_source.c"
#include "pgalloc_source.c"
#include "pgalloc_host_fixture.h"
__asm__(".globl __bss_end\n.set __bss_end, 0x180000\n"
        ".globl __kernel_resident_end\n.set __kernel_resident_end, 0x261000\n"
        ".globl __ledger_fixed_start\n.set __ledger_fixed_start, 0x17E000\n"
        ".globl __ledger_fixed_end\n.set __ledger_fixed_end, 0x180000\n");
static void say(const char *s, u32 n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void die(int rc) __attribute__((noreturn));
static void die(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
#define CHECK(x) do { \
    __asm__ volatile("" : : : "memory"); \
    if (!(x)) { static const char msg[] = "FAIL " #x "\n"; \
        say(msg, sizeof(msg) - 1); die(1); } \
} while (0)
static u32 pd[PDE_COUNT] __attribute__((aligned(PAGE_SIZE)));
static u32 active_pd[PDE_COUNT] __attribute__((aligned(PAGE_SIZE)));
static u32 pts[2][PTE_COUNT] __attribute__((aligned(PAGE_SIZE)));
static u32 flushes, watch_first, watch_end, watch_rid;
static u32 irq_countdown, requested_owner = LEDGER_OWNER_GFX;
static int watch_unmap;
static u32 raw(u32 p) { return page_tables[p / PTE_COUNT][p % PTE_COUNT]; }
static void host_irq_boundary(void)
{
    /* First save is ledger's read-only prepare; second is paging's outer
     * transaction boundary. Model a PTE change between the two passes. */
    if (irq_countdown && !--irq_countdown)
        pts[1][PTE_COUNT - 1] ^= PTE_PRESENT;
}
static void host_flush(void)
{
    CHECK(!_irq_enabled());
    for (u32 p = watch_first; p < watch_end; p++) {
        CHECK(raw(p) == (watch_unmap ? 0 : (p << PAGE_SHIFT) | PAGE_RW | PTE_PCD));
        /* The ledger changes only after all PTE writes and the flush. */
        CHECK(ledger_resource_page_mapped(watch_rid, p) == watch_unmap);
    }
    flushes++;
}
static int change(u32 rid, u32 first, u32 end, int unmap)
{
    u32 before = flushes, flags = host_arch_if;
    watch_first = first; watch_end = end; watch_rid = rid; watch_unmap = unmap;
    int rc = unmap ? paging_device_unmap(requested_owner, rid, first, end) :
                     paging_device_map(requested_owner, rid, first, end);
    CHECK(host_arch_if == flags);
    CHECK(flushes == before + (rc == 0));
    return rc;
}
static void refused(u32 rid, u32 first, u32 end, int unmap)
{
    static u32 old_pts[2][PTE_COUNT], old_pd[PDE_COUNT];
    struct ledger_device_map maps[LEDGER_MAX_DEVICE_MAPS];
    for (u32 i = 0; i < 2 * PTE_COUNT; i++) old_pts[i / PTE_COUNT][i % PTE_COUNT] = pts[i / PTE_COUNT][i % PTE_COUNT];
    for (u32 i = 0; i < PDE_COUNT; i++) old_pd[i] = pd[i];
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++) maps[i] = ledger_device_maps[i];
    CHECK(change(rid, first, end, unmap) == -1);
    for (u32 i = 0; i < 2 * PTE_COUNT; i++) CHECK(old_pts[i / PTE_COUNT][i % PTE_COUNT] == pts[i / PTE_COUNT][i % PTE_COUNT]);
    for (u32 i = 0; i < PDE_COUNT; i++) CHECK(old_pd[i] == pd[i]);
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++) {
        CHECK(maps[i].owner == ledger_device_maps[i].owner && maps[i].rid == ledger_device_maps[i].rid);
        CHECK(maps[i].first == ledger_device_maps[i].first && maps[i].end == ledger_device_maps[i].end);
    }
}
void _start(void)
{
    const u32 top = PAGING_PFN_COUNT, base = top - 2 * PTE_COUNT;
    struct ledger_resource rec = {.decode_first = base, .decode_end = top,
        .map_first = base, .map_end = base + 1,
        .bus = LEDGER_BUS_PCI, .width_basis = LEDGER_WB_SIZING};
    u32 rid;
    page_directory = pd; host_cr3 = V2P(pd); pg_enabled = 1;
    host_pool_boot(16384);
    CHECK(ledger_resource_add(&rec, &rid));
    struct ledger_span span = {base, top, LEDGER_SPAN_MMIO, rid};
    CHECK(ledger_reserve_set(LEDGER_OWNER_GFX, &span, 1));
    page_directory = pd; host_cr3 = V2P(pd); pg_enabled = 1;
    for (u32 i = 0; i < 2; i++) {
        page_tables[PDE_COUNT - 2 + i] = pts[i];
        pd[PDE_COUNT - 2 + i] = V2P(pts[i]) | PAGE_RW;
        active_pd[PDE_COUNT - 2 + i] = pd[PDE_COUNT - 2 + i];
    }
    refused(rid, base, base, 0);
    refused(rid, top, top + 1, 0);
    refused(rid, base + 3, base + 2, 0);
    refused(rid, base, base + 1, 0); /* static overlap */
    /* Missing/malformed shared PT/PDE, including the last page of a span. */
    page_tables[PDE_COUNT - 1] = 0;
    refused(rid, top - PTE_COUNT - 1, top - PTE_COUNT + 1, 0);
    page_tables[PDE_COUNT - 1] = pts[1];
    u32 bad_pde[] = {PTE_PRESENT, PTE_RW, PTE_PCD, PTE_PWT, PTE_PS, PAGE_SIZE};
    for (u32 i = 0; i < sizeof(bad_pde) / sizeof(bad_pde[0]); i++) {
        pd[PDE_COUNT - 1] ^= bad_pde[i];
        refused(rid, top - 2, top, 0);
        pd[PDE_COUNT - 1] ^= bad_pde[i];
    }
    pts[1][PTE_COUNT - 1] = PAGE_RW;
    refused(rid, top - 2, top, 0); /* only the final PTE is present */
    pts[1][PTE_COUNT - 1] = 0;
    pg_enabled = 0; refused(rid, top - 2, top, 0); pg_enabled = 1;
    /* A non-master caller must still use the registered shared PT. */
    host_cr3 = V2P(active_pd); live_addrspaces = 1;
    active_pd[PDE_COUNT - 1] ^= PAGE_SIZE;
    refused(rid, top - 2, top, 0);
    active_pd[PDE_COUNT - 1] ^= PAGE_SIZE;
    requested_owner = LEDGER_OWNER_KERNEL;
    refused(rid, top - 2, top, 0);
    requested_owner = LEDGER_OWNER_GFX;
    irq_countdown = 2;
    CHECK(change(rid, top - 2, top, 0) == -1);
    CHECK(raw(top - 2) == 0 && raw(top - 1) == PTE_PRESENT);
    CHECK(!ledger_resource_page_mapped(rid, top - 1));
    pts[1][PTE_COUNT - 1] = 0;
    CHECK(change(rid, top - 2, top, 0) == 0);
    CHECK(ledger_resource_page_mapped(rid, top - 1));
    refused(rid, top - 2, top, 0); /* dynamic overlap */
    refused(rid, top - 2, top - 1, 1); /* exact tuple only */
    requested_owner = LEDGER_OWNER_KERNEL;
    refused(rid, top - 2, top, 1);
    requested_owner = LEDGER_OWNER_GFX;
    irq_countdown = 2;
    CHECK(change(rid, top - 2, top, 1) == -1);
    CHECK(raw(top - 2) == ((top - 2) << PAGE_SHIFT | PAGE_RW | PTE_PCD));
    CHECK(raw(top - 1) == ((top - 1) << PAGE_SHIFT | PTE_RW | PTE_PCD));
    CHECK(ledger_resource_page_mapped(rid, top - 1));
    pts[1][PTE_COUNT - 1] |= PTE_PRESENT;
    u32 bad_pte[] = {PTE_PRESENT, PTE_RW, PTE_USER, PTE_PCD, PTE_PWT, PTE_PS, PAGE_SIZE};
    for (u32 i = 0; i < sizeof(bad_pte) / sizeof(bad_pte[0]); i++) {
        pts[1][PTE_COUNT - 1] ^= bad_pte[i];
        refused(rid, top - 2, top, 1);
        pts[1][PTE_COUNT - 1] ^= bad_pte[i];
    }
    pts[1][PTE_COUNT - 1] |= PTE_ACCESSED | PTE_DIRTY;
    _disable();
    CHECK(change(rid, top - 2, top, 1) == 0);
    CHECK(!ledger_resource_page_mapped(rid, top - 1));
    _enable();
    /* Two-PDE transaction and separate adjacent windows; capacity refusal. */
    u32 middle = top - PTE_COUNT;
    CHECK(change(rid, middle - 1, middle + 1, 0) == 0);
    CHECK(change(rid, middle + 1, middle + 2, 0) == 0);
    refused(rid, middle - 1, middle + 2, 1);
    CHECK(change(rid, middle + 1, middle + 2, 1) == 0);
    CHECK(change(rid, middle - 1, middle + 1, 1) == 0);
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++)
        CHECK(change(rid, base + 2 + i * 2, base + 3 + i * 2, 0) == 0);
    refused(rid, top - 2, top, 0);
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++)
        CHECK(change(rid, base + 2 + i * 2, base + 3 + i * 2, 1) == 0);
    CHECK(change(rid, top - 2, top, 0) == 0);
    CHECK(change(rid, top - 2, top, 1) == 0);
    say("PASS paging device transactions\n", sizeof("PASS paging device transactions\n") - 1);
    die(0);
}
