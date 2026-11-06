/* GF_GUARD_LIFETIME_DESIGN §7 A1-A3. Real MM, boot reservation,
 * resource lookup, BAR2 adapter and push adapter; no GPU or libc.
 * Lifetime model: MIT nouveau nouveau_bo.c:669-689 / instmem/nv40.c:238-253.
 * Synthetic PCI discovery replaces hardware only; expected window membership
 * below is explicit and independent of the product ledger query. */
unsigned int host_arch_if = 0x202U;
#define E10C_REAL_AUDIT
#define run e4_run
#include "gfx_kernel_fb_host.c"
#undef run
#include "guard_platform.c"
#include "../../hwdev/gf/command.h"
#include "shlib.h"
int kselftest_run_audit(const char *tag)
{ (void)tag; return paging_master_audit(exec_tramp_page_addr()); }

/* Keep assertions observable even when compiled with optimization. */
#undef CHECK
#define CHECK(x) do { __asm__ volatile("" : : : "memory"); \
    if (!(x)) { SAY("FAIL " #x); die(1); } } while (0)

int gf_production_selected(void) { return 1; }
int gf_production_identify(struct gf_production_resources *out)
{
    const u32 base = MEM_MMIO_TOP_PT_BASE;
    *out = (struct gf_production_resources){0};
    out->bars[0] = (struct recover_bar_extent){0, base + 128 * PAGE_SIZE, 16 * PAGE_SIZE, 1};
    out->bars[1] = (struct recover_bar_extent){0, base + 256 * PAGE_SIZE, 256 * PAGE_SIZE, 1};
    out->bars[2] = (struct recover_bar_extent){0, base + 768 * PAGE_SIZE, 16 * PAGE_SIZE, 1};
    return 0;
}
static u32 *pte(u32 addr)
{ return &page_tables[addr >> PDE_SHIFT][(addr >> PAGE_SHIFT) % PTE_COUNT]; }
static u32 map_count(void)
{
    u32 count = 0;
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++) count += !!ledger_device_maps[i].end;
    return count;
}
static void audit_ok(void)
{
    CHECK(!paging_master_audit(exec_tramp_page_addr()));
    if (space.pd_phys) {
        CHECK(!paging_as_audit(&space, shlib_read_page));
        CHECK(!lease_audit_all());
    }
}
static void refused(const struct recover_bar_extent *bar, u32 bytes)
{
    static u32 saved_pt[PTE_COUNT];
    struct ledger_device_map saved[LEDGER_MAX_DEVICE_MAPS];
    volatile u8 *out = P2V_IO(0x12345000U);
    for (u32 i = 0; i < PTE_COUNT; i++) saved_pt[i] = page_tables[bar->base >> PDE_SHIFT][i];
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++) saved[i] = ledger_device_maps[i];
    CHECK(gf_production_instmem_map_ops.map(0, bar, bytes, &out) == -1);
    CHECK(out == P2V_IO(0x12345000U));
    for (u32 i = 0; i < PTE_COUNT; i++) CHECK(saved_pt[i] == page_tables[bar->base >> PDE_SHIFT][i]);
    for (u32 i = 0; i < sizeof(saved); i++)
        CHECK(((u8 *)saved)[i] == ((u8 *)ledger_device_maps)[i]);
}
static void run(void) __attribute__((used));
static void run(void)
{
    map_host(0x10000, 0xFF0000);
    boot(); gfx_set_backend_pref(GFX_PREF_PC98); gfx_boot_reserve();
    CHECK(reserve()); /* Real platform reserve, including shared PT preparation. */
    paging_reclaim_conventional();
    CHECK(!paging_set_not_present(MEM_SHM_GUARD_LO, MEM_SHM_GUARD_LO + PAGE_SIZE - 1));
    CHECK(!paging_set_not_present(MEM_SHM_GUARD_HI, MEM_SHM_GUARD_HI + PAGE_SIZE - 1));
    CHECK(!paging_boot_user_shared(exec_tramp_page_addr()));
    audit_ok();
    u32 owner;
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "gf-guard", &owner));
    CHECK(!paging_addrspace_create_lease(&space, owner));
    slot.as = &space;
    struct gf_foundation f = {.map_ops = &gf_production_instmem_map_ops};
    struct gf2_chan ch = {.map_ops = &gf_production_instmem_map_ops,
                         .bar1 = boot_resources.bars[1]};
    CHECK(!ch.f && !ch.mapped && !f.mapped && !map_count());
    CHECK(gf_instmem_map(&f, &boot_resources.bars[2], 2 * PAGE_SIZE) == GF1_OK);
    volatile u32 *push = 0;
    const u32 display_pages = PAGE_ALIGN_UP(GF_MODE_VRAM_BYTES) / PAGE_SIZE;
    CHECK(gf2_map_vram(&ch, (display_pages + 4) * PAGE_SIZE, GF2_PUSH_BYTES, &push) == GF2_OK);
    const u32 offsets[] = {display_pages + 40, display_pages + 80};
    volatile u8 *views[2];
    struct recover_bar_extent view[2];
    for (u32 i = 0; i < 2; i++) {
        view[i] = ch.bar1; view[i].base += offsets[i] * PAGE_SIZE; view[i].length = 2 * PAGE_SIZE;
        CHECK(!gf_production_instmem_map_ops.map(0, &view[i], view[i].length, &views[i]));
        audit_ok();
    }
    CHECK(map_count() == 4);
    /* DISPLAY, push, and two distant views. Every intervening page stays NP. */
    for (u32 p = 0; p < ch.bar1.length / PAGE_SIZE; p++) {
        u32 addr = ch.bar1.base + p * PAGE_SIZE, saved = *pte(addr);
        int mapped = p < display_pages ||
            (p >= display_pages + 4 && p < display_pages + 4 + GF2_PUSH_BYTES / PAGE_SIZE) ||
            (p >= offsets[0] && p < offsets[0] + 2) || (p >= offsets[1] && p < offsets[1] + 2);
        CHECK(!!(saved & PTE_PRESENT) == mapped);
        if (!mapped) {
            *pte(addr) = addr | PAGE_RW | PTE_PCD;
            CHECK(paging_master_audit(exec_tramp_page_addr()) > 0);
        } else {
            const u32 corrupt[] = {PTE_PRESENT, PTE_USER, PTE_PCD, PAGE_SIZE};
            for (u32 i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); i++) {
                *pte(addr) = saved ^ corrupt[i];
                CHECK(paging_master_audit(exec_tramp_page_addr()) > 0);
            }
        }
        *pte(addr) = saved;
    }
    audit_ok();
    refused(&view[0], view[0].length); /* overlap, out/PTE/ledger unchanged */
    u32 rid, resource_owner;
    CHECK(!geforce_resource_lookup(view[0].base, view[0].length, &resource_owner, &rid));
    CHECK(paging_device_map(LEDGER_OWNER_KERNEL, rid, view[0].base / PAGE_SIZE + 3,
                            view[0].base / PAGE_SIZE + 4) == -1);
    /* A real unmap preflight failure must keep adapter state and registration. */
    u32 saved = *pte(V2P(f.bar2) + PAGE_SIZE);
    *pte(V2P(f.bar2) + PAGE_SIZE) ^= PTE_USER;
    CHECK(gf_instmem_unmap(&f) == GF1_EMAP);
    CHECK(f.mapped && f.bar2 && f.imem.aperture_bytes == 2 * PAGE_SIZE && map_count() == 4);
    *pte(V2P(f.bar2) + PAGE_SIZE) = saved;
    CHECK(gf_instmem_unmap(&f) == GF1_OK && map_count() == 3);
    CHECK(gf2_unmap_vram(&ch) == GF2_OK);
    for (u32 i = 0; i < 2; i++) CHECK(!gf_production_instmem_map_ops.unmap(0, views[i], view[i].length));
    CHECK(!map_count()); audit_ok();
    /* Fill all 64 slots through the production adapter, then refuse slot 65. */
    struct recover_bar_extent one = ch.bar1;
    one.length = PAGE_SIZE;
    volatile u8 *ptrs[LEDGER_MAX_DEVICE_MAPS];
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++) {
        one.base = ch.bar1.base + (display_pages + 2 * i) * PAGE_SIZE;
        CHECK(!gf_production_instmem_map_ops.map(0, &one, PAGE_SIZE, &ptrs[i]));
    }
    CHECK(map_count() == LEDGER_MAX_DEVICE_MAPS);
    one.base += 2 * PAGE_SIZE; refused(&one, PAGE_SIZE);
    for (u32 i = 0; i < LEDGER_MAX_DEVICE_MAPS; i++)
        CHECK(!gf_production_instmem_map_ops.unmap(0, ptrs[i], PAGE_SIZE));
    CHECK(!gf_production_instmem_map_ops.map(0, &one, PAGE_SIZE, &ptrs[0]));
    CHECK(!gf_production_instmem_map_ops.unmap(0, ptrs[0], PAGE_SIZE));
    CHECK(!map_count()); audit_ok();
    paging_addrspace_destroy(&space);
    CHECK(!space.pd_phys);
    SAY("PASS gf guard A1/A2/A3 real MM/production map/capacity/retry");
    die(0);
}
