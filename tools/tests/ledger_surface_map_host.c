/* M4: GF_GUARD_LIFETIME_DESIGN §2 / A1-A2 surface membership.
 * Real allocator/resource/surface code; synthetic PFNs, privileged IRQ seam.
 * This OS32-specific ledger guard has no nouveau equivalent (§2). */
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

static void window(u32 rid, u32 first, u32 end, int unmap)
{
    struct ledger_device_map map = {LEDGER_OWNER_GFX, rid, first, end};
    unsigned int flags = irq_save();
    u32 slot;
    CHECK(ledger_device_map_prepare(map.owner, rid, first, end, unmap, &slot) == 0);
    ledger_device_map_commit(slot, unmap ? 0 : &map);
    irq_restore(flags);
}

void _start(void)
{
    const u32 base = PHYSMEM_MAX_PFN - 64;
    struct ledger_resource rec = {
        .decode_first = base, .decode_end = PHYSMEM_MAX_PFN,
        .map_first = base, .map_end = base + 2,
        .bus = LEDGER_BUS_PCI, .width_basis = LEDGER_WB_SIZING
    };
    struct ledger_surface sf = {
        .first = base, .npages = 1, .owner = LEDGER_OWNER_GFX,
        .backing = LEDGER_SB_MMIO, .cache = LEDGER_CACHE_UC,
        .planes = 1, .width = 16, .height = 1, .pitch = 64,
        .perm_max = LEDGER_PERM_RW
    };
    u32 rid, alias;
    host_pool_boot(16384);
    CHECK(ledger_resource_add(&rec, &rid));
    rec.map_first = rec.map_end = 0;
    CHECK(ledger_resource_add(&rec, &alias));
    struct ledger_span span = {base, PHYSMEM_MAX_PFN, LEDGER_SPAN_MMIO, rid};
    CHECK(ledger_reserve_set(LEDGER_OWNER_GFX, &span, 1));
    window(rid, base + 8, base + 10, 0);
    window(rid, base + 16, base + 18, 0);
    window(rid, PHYSMEM_MAX_PFN - 2, PHYSMEM_MAX_PFN, 0);

    /* Every page in the decode is checked, including all holes and ends.
     * Both VRAM and MMIO backed surfaces use device resource membership. */
    for (u32 backing = LEDGER_SB_VRAM; backing <= LEDGER_SB_MMIO; backing++) {
        sf.backing = backing;
        for (u32 p = base; p < PHYSMEM_MAX_PFN; p++) {
            int expected = p < base + 2 || (base + 8 <= p && p < base + 10) ||
                (base + 16 <= p && p < base + 18) || p >= PHYSMEM_MAX_PFN - 2;
            sf.first = p;
            unsigned int flags = host_arch_if;
            CHECK(ledger_surface_validate(&sf) == expected);
            CHECK(host_arch_if == flags);
        }
    }
    sf.first = base + 8;
    sf.npages = 2;
    CHECK(ledger_surface_validate(&sf));
    sf.npages = 3; /* Starts mapped, crosses into an unmapped hole. */
    CHECK(!ledger_surface_validate(&sf));
    sf.npages = 1;
    sf.cache = LEDGER_CACHE_WB;
    CHECK(!ledger_surface_validate(&sf));
    sf.cache = LEDGER_CACHE_UC;

    /* A mapped alias is insufficient: the DEVICE region must name its rid. */
    ledger_regions[0].res_mask = 1U << alias;
    CHECK(!ledger_surface_validate(&sf));
    ledger_regions[0].res_mask = 1U << rid;
    CHECK(ledger_surface_validate(&sf));
    window(rid, base + 8, base + 10, 1);
    CHECK(!ledger_surface_validate(&sf));
    sf.first = base + 16;
    CHECK(ledger_surface_validate(&sf));
    sf.first = base;
    CHECK(ledger_surface_validate(&sf));
    /* Nested helper IRQ save must also preserve a caller's disabled IF. */
    host_arch_if &= ~X86_EFLAGS_IF;
    CHECK(ledger_surface_validate(&sf));
    CHECK(!_irq_enabled());
    static const char pass[] = "PASS surface static/dynamic union, holes, resource, unmap, cache, IRQ\n";
    say(pass, sizeof(pass) - 1);
    die(0);
}
