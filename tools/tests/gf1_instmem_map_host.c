/* GF_GUARD_LIFETIME_DESIGN §2, H4: real resource lookup + adapter + MM.
 * Reference: nouveau c03c1a1, nvkm/subdev/instmem/nv40.c:238-253 and
 * nouveau_bo.c:669-689. OS32 adds exact, atomic PTE/ledger windows.
 * Synthetic boot reservations below are not a hardware register fixture. */
#include "types.h"
static u32 host_cr3;
#include "paging_source.c"
#include "pgalloc_source.c"
#include "pgalloc_host_fixture.h"
#include "geforce_platform_source.c"
#include "../../hwdev/gf/foundation.h"
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
#define PASS(s) say("PASS " s "\n", sizeof("PASS " s "\n") - 1)
static u32 pd[PDE_COUNT] __attribute__((aligned(PAGE_SIZE)));
static u32 pts[RECOVER_BAR_COUNT][PTE_COUNT] __attribute__((aligned(PAGE_SIZE)));
static u32 map_count(void)
{
    u32 count = 0;
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++)
        if (ledger_device_maps[i].end) count++;
    return count;
}
/* Failure must preserve every PTE, registration and caller-owned field,
 * including an unpublished direct adapter output. */
static void refused(struct gf_foundation *f, struct recover_bar_extent *b,
                    u32 bytes, int unmap)
{
    static u32 old_pts[RECOVER_BAR_COUNT][PTE_COUNT];
    struct ledger_device_map maps[LEDGER_MAX_DEVICE_MAPS];
    struct gf_foundation before = *f;
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++)
        for (u32 j = 0; j < PTE_COUNT; j++) old_pts[i][j] = pts[i][j];
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++) maps[i] = ledger_device_maps[i];
    CHECK((unmap ? gf_instmem_unmap(f) : gf_instmem_map(f, b, bytes)) == GF1_EMAP);
    CHECK(f->mapped == before.mapped && f->bar2 == before.bar2 &&
          f->imem.aperture_bytes == before.imem.aperture_bytes);
    if (!unmap) {
        volatile u8 *out = (volatile u8 *)1;
        CHECK(f->map_ops->map(0, b, bytes, &out) != 0 && out == (volatile u8 *)1);
    }
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++)
        for (u32 j = 0; j < PTE_COUNT; j++) CHECK(pts[i][j] == old_pts[i][j]);
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++) {
        CHECK(maps[i].owner == ledger_device_maps[i].owner && maps[i].rid == ledger_device_maps[i].rid);
        CHECK(maps[i].first == ledger_device_maps[i].first && maps[i].end == ledger_device_maps[i].end);
    }
}
static void lookup_refused(u32 base, u32 bytes)
{
    u32 owner = ~0U, rid = ~0U;
    CHECK(geforce_resource_lookup(base, bytes, &owner, &rid) == -1);
    CHECK(owner == ~0U && rid == ~0U);
}
void _start(void)
{
    host_pool_boot(16384);
    page_directory = pd; host_cr3 = V2P(pd); pg_enabled = 1;
    /* Model the successful boot reservation result. Real lookup consumes
     * the saved IDs; real ledger validates ownership and UC coverage.
     * Reverse BAR/resource order so an index mistaken for rid is caught. */
    struct ledger_span spans[RECOVER_BAR_COUNT];
    for (u32 n = RECOVER_BAR_COUNT; n; n--) {
        u32 i = n - 1, base = 0x24000000 + i * MEM_DEVICE_APERTURE_PDE_SIZE;
        boot_resources.bars[i] = (struct recover_bar_extent){0, base, 0x100000, 1};
        u32 first = base / PAGE_SIZE, rid;
        struct ledger_resource r = {.decode_first = first, .decode_end = first + 256,
            .bus = LEDGER_BUS_PCI, .width_basis = LEDGER_WB_SIZING};
        CHECK(ledger_resource_add(&r, &rid));
        spans[i] = (struct ledger_span){first, first + 256, LEDGER_SPAN_MMIO, rid};
        boot_resource_ids[i] = rid;
        page_tables[base >> PDE_SHIFT] = pts[i];
        pd[base >> PDE_SHIFT] = V2P(pts[i]) | PAGE_RW;
    }
    CHECK(ledger_reserve_set(LEDGER_OWNER_GFX, spans, RECOVER_BAR_COUNT));
    struct recover_bar_extent b = boot_resources.bars[2];
    struct gf_foundation f = {.map_ops = &gf_production_instmem_map_ops};
    lookup_refused(b.base, PAGE_SIZE); /* unpublished boot IDs */
    refused(&f, &b, 2 * PAGE_SIZE, 0);
    boot_resources_valid = true;
    lookup_refused(b.base, 0);
    lookup_refused(~0U - PAGE_SIZE + 1, PAGE_SIZE);
    lookup_refused(b.base + b.length, PAGE_SIZE);
    lookup_refused(b.base + b.length - PAGE_SIZE, 2 * PAGE_SIZE);
    struct recover_bar_extent saved = boot_resources.bars[0];
    boot_resources.bars[0] = b;
    lookup_refused(b.base, PAGE_SIZE); /* ambiguous containment */
    refused(&f, &b, 2 * PAGE_SIZE, 0);
    boot_resources.bars[0] = saved;
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++) {
        u32 owner = ~0U, rid = ~0U;
        CHECK(!geforce_resource_lookup(boot_resources.bars[i].base + PAGE_SIZE,
                                     PAGE_SIZE, &owner, &rid));
        CHECK(owner == LEDGER_OWNER_GFX && rid == boot_resource_ids[i]);
    }
    PASS("real rid lookup: publication, containment, overflow, ambiguity");
    CHECK(gf_instmem_map(&f, &b, b.length + PAGE_SIZE) == GF1_EMAP);
    b.valid = 0; CHECK(gf_instmem_map(&f, &b, PAGE_SIZE) == GF1_EMAP); b.valid = 1;
    b.base++; CHECK(gf_instmem_map(&f, &b, PAGE_SIZE) == GF1_EMAP); b.base--;
    pts[2][1] = PAGE_RW; /* final PTE occupied: map preflight failure */
    refused(&f, &b, 2 * PAGE_SIZE, 0);
    pts[2][1] = 0;
    u32 rid = boot_resource_ids[2];
    boot_resource_ids[2] = LEDGER_MAX_RESOURCES;
    refused(&f, &b, 2 * PAGE_SIZE, 0); /* lookup success is not MM admission */
    boot_resource_ids[2] = rid;
    CHECK(ledger_resource_set_map(rid, b.base / PAGE_SIZE, b.base / PAGE_SIZE + 1));
    pts[2][0] = b.base | PAGE_RW | PTE_PCD;
    refused(&f, &b, 2 * PAGE_SIZE, 0); /* static overlap */
    pts[2][0] = 0;
    CHECK(ledger_resource_set_map(rid, 0, 0));
    /* Static DISPLAY and three separate dynamic tail windows share BAR1.
     * H1 owns the complete master/AS/lease audit; here check exact membership. */
    u32 vrid = boot_resource_ids[1], first = boot_resources.bars[1].base / PAGE_SIZE;
    CHECK(ledger_resource_set_map(vrid, first, first + 1));
    pts[1][0] = first * PAGE_SIZE | PAGE_RW | PTE_PCD;
    struct gf_foundation tail[3] = {0};
    for (u32 i = 0; i < 3; i++) {
        tail[i].map_ops = f.map_ops;
        struct recover_bar_extent v = boot_resources.bars[1];
        v.base += (2 + 4 * i) * PAGE_SIZE; v.length = 2 * PAGE_SIZE;
        CHECK(gf_instmem_map(&tail[i], &v, v.length) == GF1_OK);
    }
    for (u32 p = 0; p < 16; p++) {
        int expected = p == 0 || p == 2 || p == 3 || p == 6 || p == 7 || p == 10 || p == 11;
        CHECK(ledger_resource_page_mapped(vrid, first + p) == expected);
        CHECK(!!(pts[1][p] & PTE_PRESENT) == expected);
    }
    for (int cycle = 0; cycle < 2; cycle++) {
        CHECK(gf_instmem_map(&f, &b, 2 * PAGE_SIZE) == GF1_OK);
        CHECK(f.mapped && V2P(f.bar2) == b.base && f.imem.aperture_bytes == 2 * PAGE_SIZE);
        CHECK(map_count() == 4);
        struct gf_foundation duplicate = {.map_ops = f.map_ops};
        refused(&duplicate, &b, 2 * PAGE_SIZE, 0); /* dynamic overlap */
        CHECK(f.map_ops->unmap(0, f.bar2, PAGE_SIZE) != 0); /* exact tuple */
        CHECK(map_count() == 4 && f.mapped);
        for (u32 p = 0; p < 2; p++) {
            CHECK(pts[2][p] == ((b.base + p * PAGE_SIZE) | PAGE_RW | PTE_PCD));
            CHECK(ledger_resource_page_mapped(rid, b.base / PAGE_SIZE + p));
        }
        /* Lookup and MM failures independently retain the map for retry. */
        boot_resources_valid = false; refused(&f, &b, 0, 1); boot_resources_valid = true;
        u32 good = pts[2][1];
        const u32 corrupt[] = {PTE_PRESENT, PTE_RW, PTE_USER, PTE_PCD, PAGE_SIZE};
        for (u32 i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); i++) {
            pts[2][1] = good ^ corrupt[i]; refused(&f, &b, 0, 1);
        }
        pts[2][1] = good;
        CHECK(gf_instmem_unmap(&f) == GF1_OK);
        CHECK(!f.mapped && !f.bar2 && !f.imem.aperture_bytes && map_count() == 3);
        CHECK(!pts[2][0] && !pts[2][1]);
        CHECK(!ledger_resource_page_mapped(rid, b.base / PAGE_SIZE));
        CHECK(gf_instmem_unmap(&f) == GF1_EMAP);
    }
    PASS("adapter map/unmap: exact PTEs, failure ownership, retry and restart");
    /* Exhaust real fixed map storage, then retry after freeing one slot. */
    struct gf_foundation many[LEDGER_MAX_DEVICE_MAPS] = {0};
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS - 3; i++) {
        struct recover_bar_extent v = b;
        v.base += (4 + 2 * i) * PAGE_SIZE; v.length = PAGE_SIZE;
        many[i].map_ops = f.map_ops;
        CHECK(gf_instmem_map(&many[i], &v, PAGE_SIZE) == GF1_OK);
    }
    CHECK(map_count() == LEDGER_MAX_DEVICE_MAPS);
    refused(&f, &b, 2 * PAGE_SIZE, 0);
    CHECK(gf_instmem_unmap(&many[0]) == GF1_OK);
    CHECK(gf_instmem_map(&f, &b, 2 * PAGE_SIZE) == GF1_OK);
    CHECK(gf_instmem_unmap(&f) == GF1_OK);
    for (u32 i = 1; i < LEDGER_MAX_DEVICE_MAPS - 3; i++) CHECK(gf_instmem_unmap(&many[i]) == GF1_OK);
    for (u32 i = 0; i < 3; i++) CHECK(gf_instmem_unmap(&tail[i]) == GF1_OK);
    CHECK(!map_count() && ledger_resource_page_mapped(vrid, first) && pts[1][0]);
    PASS("64-map capacity, independent windows, static DISPLAY preserved");
    die(0);
}
