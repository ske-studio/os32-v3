/* M1 acceptance: GF_GUARD_LIFETIME_DESIGN §2, A1/A2 membership and A3
 * transactions. Real pgalloc/resource code, privileged IRQ seam only.
 * PFNs below are synthetic decode ranges; no external hardware fixture. */
#include "pgalloc.c"
#include "pgalloc_host_fixture.h"

int paging_boot_context(void) { return 1; }

static void say(const char *s, u32 n)
{
    u32 call = 4;
    __asm__ volatile("int $0x80" : "+a"(call) : "b"(1), "c"(s), "d"(n) : "memory");
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
#define PASS(s) say("PASS " s "\n", sizeof("PASS " s "\n") - 1)

/* Observe failed prepare without relying on a single membership sample. */
static void refused(u32 owner, u32 rid, u32 first, u32 end, int unmap)
{
    struct ledger_device_map before[LEDGER_MAX_DEVICE_MAPS];
    u32 slot = LEDGER_MAX_DEVICE_MAPS + 1;
    unsigned int flags = host_arch_if;
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++) before[i] = ledger_device_maps[i];
    CHECK(ledger_device_map_prepare(owner, rid, first, end, unmap, &slot) == -1);
    CHECK(slot == LEDGER_MAX_DEVICE_MAPS + 1 && host_arch_if == flags);
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++) {
        CHECK(before[i].owner == ledger_device_maps[i].owner);
        CHECK(before[i].rid == ledger_device_maps[i].rid);
        CHECK(before[i].first == ledger_device_maps[i].first);
        CHECK(before[i].end == ledger_device_maps[i].end);
    }
}
static u32 open_window(u32 rid, u32 first, u32 end)
{
    struct ledger_device_map m = {LEDGER_OWNER_GFX, rid, first, end};
    unsigned int flags = irq_save();
    u32 slot;
    CHECK(ledger_device_map_prepare(m.owner, rid, first, end, 0, &slot) == 0);
    CHECK(!_irq_enabled());
    /* prepare is read-only, publication belongs to commit. */
    CHECK(!ledger_device_maps[slot].end);
    ledger_device_map_commit(slot, &m);
    CHECK(!_irq_enabled());
    irq_restore(flags);
    return slot;
}
static void close_window(u32 rid, u32 first, u32 end)
{
    unsigned int flags = irq_save();
    u32 slot;
    CHECK(ledger_device_map_prepare(LEDGER_OWNER_GFX, rid, first, end, 1, &slot) == 0);
    ledger_device_map_commit(slot, 0);
    CHECK(!ledger_device_maps[slot].end && !_irq_enabled());
    irq_restore(flags);
}
void _start(void)
{
    /* MMIO lives outside the host RAM pool, up to the last 4GiB page. */
    const u32 base = PHYSMEM_MAX_PFN - 512;
    struct ledger_resource rec = {
        .decode_first = base, .decode_end = PHYSMEM_MAX_PFN,
        .map_first = base, .map_end = base + 4,
        .bus = LEDGER_BUS_PCI, .width_basis = LEDGER_WB_SIZING
    };
    u32 rid, alias, other, as_owner, slot;
    host_pool_boot(16384);
    CHECK(ledger_resource_add(&rec, &rid));
    /* An overlapping resource is legal provenance, but its static mapping
     * must still block a dynamic physical alias through the first record. */
    rec.map_first = base + 8; rec.map_end = base + 10;
    CHECK(ledger_resource_add(&rec, &alias));
    rec.map_first = rec.map_end = 0;
    CHECK(ledger_resource_add(&rec, &other));
    struct ledger_span span = {base, PHYSMEM_MAX_PFN, LEDGER_SPAN_MMIO, rid};
    CHECK(ledger_reserve_set(LEDGER_OWNER_GFX, &span, 1));
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 1, "as", &as_owner));
    CHECK(ledger_owner_new(LEDGER_KIND_DEVICE, 1, "device", &other));

    refused(0, rid, base + 12, base + 14, 0);
    refused(LEDGER_MAX_OWNERS, rid, base + 12, base + 14, 0);
    refused(as_owner, rid, base + 12, base + 14, 0);
    /* Independently exercise kind validation at the ledger trust boundary. */
    ledger_regions[0].owner = as_owner;
    refused(as_owner, rid, base + 12, base + 14, 0);
    ledger_regions[0].owner = LEDGER_OWNER_GFX;
    refused(other, rid, base + 12, base + 14, 0);
    refused(LEDGER_OWNER_GFX, LEDGER_MAX_RESOURCES, base + 12, base + 14, 0);
    refused(LEDGER_OWNER_GFX, LEDGER_MAX_RESOURCES - 1, base + 12, base + 14, 0);
    refused(LEDGER_OWNER_GFX, rid, base + 12, base + 12, 0);
    refused(LEDGER_OWNER_GFX, rid, base + 14, base + 12, 0);
    refused(LEDGER_OWNER_GFX, rid, base - 1, base + 1, 0);
    refused(LEDGER_OWNER_GFX, rid, base + 12, PHYSMEM_MAX_PFN + 1, 0);
    CHECK(ledger_device_map_prepare(LEDGER_OWNER_GFX, rid, base + 12, base + 14, 0, 0) == -1);
    refused(LEDGER_OWNER_GFX, alias, base + 12, base + 14, 0); /* res_mask */
    ledger_regions[0].cache = LEDGER_CACHE_WB;
    refused(LEDGER_OWNER_GFX, rid, base + 12, base + 14, 0);
    ledger_regions[0].cache = LEDGER_CACHE_UC;
    u32 saved_end = ledger_regions[0].end;
    ledger_regions[0].end = base + 13;
    refused(LEDGER_OWNER_GFX, rid, base + 12, base + 14, 0);
    ledger_regions[0].end = saved_end;
    ledger_resources[rid].width_basis = LEDGER_WB_RAW;
    refused(LEDGER_OWNER_GFX, rid, base + 12, base + 14, 0);
    ledger_resources[rid].width_basis = LEDGER_WB_SIZING;
    online = 0;
    refused(LEDGER_OWNER_GFX, rid, base + 12, base + 14, 0);
    online = 1;
    refused(LEDGER_OWNER_GFX, rid, base + 2, base + 6, 0); /* static */
    refused(LEDGER_OWNER_GFX, rid, base + 9, base + 12, 0); /* alias static */
    PASS("reservation/owner/cache/decode/static validation");

    const u32 starts[] = {base + 12, base + 16, base + 20, PHYSMEM_MAX_PFN - 2};
    for (u32 i = 0; i < sizeof(starts) / sizeof(starts[0]); i++)
        open_window(rid, starts[i], starts[i] + 2);
    for (u32 p = base - 1; p <= PHYSMEM_MAX_PFN; p++) {
        int expected = (base <= p && p < base + 4);
        for (u32 i = 0; i < sizeof(starts) / sizeof(starts[0]); i++)
            if (p == starts[i] || p == starts[i] + 1) expected = 1;
        CHECK(ledger_resource_page_mapped(rid, p) == expected);
    }
    CHECK(!ledger_resource_page_mapped(LEDGER_MAX_RESOURCES, base));
    CHECK(!ledger_resource_page_mapped(LEDGER_MAX_RESOURCES - 1, base));
    CHECK(!ledger_resource_page_mapped(alias, base + 12));
    CHECK(!ledger_owner_retire(LEDGER_OWNER_GFX));
    refused(LEDGER_OWNER_GFX, rid, base + 12, base + 14, 0);
    refused(LEDGER_OWNER_GFX, rid, base + 13, base + 15, 0);
    refused(LEDGER_OWNER_GFX, rid, base + 11, base + 13, 0);
    /* Complete identity matters: subsets, supersets and adjacent unions fail. */
    refused(LEDGER_OWNER_GFX, rid, base + 12, base + 13, 1);
    refused(LEDGER_OWNER_GFX, rid, base + 12, base + 15, 1);
    refused(other, rid, base + 12, base + 14, 1);
    refused(LEDGER_OWNER_GFX, alias, base + 12, base + 14, 1);
    CHECK(ledger_resource_set_map(rid, base, base + 4)); /* unchanged is safe */
    CHECK(!ledger_resource_set_map(rid, 0, 0));
    CHECK(!ledger_resource_set_map(rid, base, base + 3));
    CHECK(!ledger_resource_set_map(alias, base + 12, base + 14));
    rec.map_first = base + 12; rec.map_end = base + 14;
    slot = LEDGER_MAX_RESOURCES;
    CHECK(!ledger_resource_add(&rec, &slot) && slot == LEDGER_MAX_RESOURCES);
    CHECK(ledger_resources[rid].map_first == base && ledger_resources[rid].map_end == base + 4);
    CHECK(ledger_resources[alias].map_first == base + 8 && ledger_resources[alias].map_end == base + 10);
    /* Failure preserved the registration; exact retry succeeds. */
    for (u32 i = 0; i < sizeof(starts) / sizeof(starts[0]); i++) {
        close_window(rid, starts[i], starts[i] + 2);
        CHECK(!ledger_resource_page_mapped(rid, starts[i]));
    }
    CHECK(ledger_resource_set_map(rid, 0, 0));
    CHECK(!ledger_resource_page_mapped(rid, base));
    CHECK(ledger_resource_set_map(alias, 0, 0));
    CHECK(ledger_resource_add(&rec, &slot)); /* same static add after close */
    CHECK(ledger_resource_set_map(slot, 0, 0));
    CHECK(ledger_resource_set_map(rid, base, base + 4));
    PASS("exact union/holes/4GiB end/static freeze/unmap retry");

    /* Adjacent registrations consume separate slots; capacity never widens
     * a window or publishes an output on failure. */
    host_arch_if &= ~X86_EFLAGS_IF;
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++) {
        slot = open_window(rid, base + 32 + i, base + 33 + i);
        CHECK(slot == i && !_irq_enabled());
    }
    refused(LEDGER_OWNER_GFX, rid, base + 128, base + 129, 0);
    refused(LEDGER_OWNER_GFX, rid, base + 32, base + 34, 1);
    close_window(rid, base + 40, base + 41);
    CHECK(open_window(rid, base + 128, base + 129) == 8);
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++)
        if (i != 8) close_window(rid, base + 32 + i, base + 33 + i);
    close_window(rid, base + 128, base + 129);
    CHECK(!_irq_enabled());
    CHECK(ledger_resource_set_map(rid, 0, 0));
    PASS("64 slots/adjacency/reuse/IRQ preservation");
    die(0);
}
