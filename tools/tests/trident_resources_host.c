/* libc-free kernel API seam. PCI boot values come from the common device fake;
 * no fake register access is allowed during this core-only transaction. */
#include "trident_device_fake.h"
#include "drivers/pci/pci.h"
#include "mm/pgalloc.h"
#include "gfx_hal.h"
#include "kprintf.h"

static void die(const char *s)
{
    u32 len = 0; while (s[len]) ++len;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(len) : "memory");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(1) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { __asm__ volatile("" ::: "memory"); if (!(x)) die("FAIL " #x "\n"); } while (0)
#ifndef TRI_RAM_FALLBACK_DEFAULT
#define TRI_RAM_FALLBACK_DEFAULT 1
#endif
static u8 ram[3u * 1024 * 1024] __attribute__((aligned(4096)));
static bool ram_live, fail_ram_once;
static struct trident_fake fake;
static u8 fake_storage[TRI_FAKE_REAL_BYTES];
/* Fixed capacity covers every exposed format; selected bytes may be smaller. */
#define DISPLAY_CAPACITY (3u * 1024 * 1024)
static u32 current_cr3 = 0x1000, irq_enabled = 1, reloads, revokes, regens;
static u32 expected_width = 640, expected_height = 480, expected_pitch = 640;
static u32 expected_format = GFX_BB_PACKED8, expected_capacity = DISPLAY_CAPACITY;
static bool fail_client_once, wrong_sid_once;
unsigned int host_irq_save(void) { unsigned int f = irq_enabled; irq_enabled = 0; return f; }
void host_irq_restore(unsigned int f) { CHECK(!irq_enabled); irq_enabled = f; }
struct ledger_resource ledger_resources[LEDGER_MAX_RESOURCES];
struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
volatile u32 kctx_irq_depth, kctx_exc_depth;
u32 gfx_surface_unready;
static u32 config[PCI_DEV_COUNT][PCI_FN_COUNT][PCI_CFG_SPACE_SIZE / 4];
static u32 mode, records, reservations, map_calls, present_maps, allocs, frees;
static u32 surface_calls, release_calls, owner_calls, config_reads, logs, map_records;
static u32 last_spans, read8_count, read16_count, read32_count;

static const u32 fxp_slot = 9; /* fixture BDF, independent of the product */
static int failure(u32 point) { return mode == point; }
u32 paging_current_cr3(void) { return failure(1) ? 0x2000 : current_cr3; }
u32 paging_kernel_pd_phys(void) { return 0x1000; }
u32 paging_live_addrspaces(void) { return failure(2); }
void paging_load_cr3(u32 cr3) { CHECK(!irq_enabled); current_cr3 = cr3; ++reloads; }
int lease_revoke_surface(u32 sid)
{
    CHECK(!irq_enabled && current_cr3 == 0x1000);
    CHECK(sid == 2 || sid == 3);
    CHECK(gfx_surface_unready & (1u << sid));
    ++revokes;
    if (failure(23)) return 1;
    if (!failure(67)) ledger_surfaces[sid].lease_count = 0;
    return 0;
}

u32 pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    ++config_reads; ++read32_count;
    CHECK(bus == 0 && dev < PCI_DEV_COUNT && fn < PCI_FN_COUNT && !(reg & 3));
    return config[dev][fn][reg / 4];
}
u16 pci_cfg_read16(u32 bus, u32 dev, u32 fn, u32 reg)
{
    ++read16_count;
    return pci_cfg_read32(bus, dev, fn, reg & ~3u) >> ((reg & 2) * 8);
}
u8 pci_cfg_read8(u32 bus, u32 dev, u32 fn, u32 reg)
{
    ++read8_count;
    return pci_cfg_read32(bus, dev, fn, reg & ~3u) >> ((reg & 3) * 8);
}
#define LFB_BASE (mode == 55 ? 0x21000000u : 0x20000000u)
#define REG_BASE (mode == 32 ? 0x20410000u : 0x20400000u)
int pgalloc_range_has_ram(u32 first, u32 end)
{
    CHECK((first == LFB_BASE / PAGE_SIZE && end == LFB_BASE / PAGE_SIZE + 1024) || (first == REG_BASE / PAGE_SIZE && end == REG_BASE / PAGE_SIZE + 16));
    return failure(10) || (failure(33) && first == 0x20400);
}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner)
{
    ++owner_calls;
    CHECK(kind == LEDGER_KIND_DEVICE && id == 0x40 && name[0] == 't');
    if (failure(11)) return 0;
    *owner = 8; return 1;
}
int ledger_resource_add(const struct ledger_resource *r, u32 *rid)
{
    ++records;
    if ((failure(12) && records == 1) || (failure(13) && records == 2)) return 0;
    CHECK(records <= 2 && r->vendor == 0x1023 && r->device == 0x9660);
    CHECK(r->bus == LEDGER_BUS_PCI && r->bdf == 0x40 && r->revision == (mode == 5 ? 0xd2 : 0xd3));
    CHECK(r->bar == records - 1 && r->width_basis == LEDGER_WB_GLUE_CONST);
    CHECK(!r->map_first && !r->map_end);
    CHECK(r->raw_bar == (records == 1 ? LFB_BASE : REG_BASE));
    CHECK(r->decode_first == (records == 1 ? LFB_BASE / PAGE_SIZE : REG_BASE / PAGE_SIZE));
    CHECK(r->decode_end == (records == 1 ? LFB_BASE / PAGE_SIZE + 1024 : REG_BASE / PAGE_SIZE + 16));
    /* Imported RAW records may already occupy slots: RID != BAR index. */
    *rid = records + 2;
    ledger_resources[*rid] = *r; return 1;
}
int ledger_reserve_set(u32 owner, const struct ledger_span *sp, u32 n)
{
    ++reservations;
    CHECK(owner == 8 && n == 2);
    CHECK(sp[0].first == LFB_BASE / PAGE_SIZE && sp[0].end == LFB_BASE / PAGE_SIZE + 1024 && sp[0].res == 3);
    CHECK(sp[1].first == REG_BASE / PAGE_SIZE && sp[1].end == REG_BASE / PAGE_SIZE + 16 && sp[1].res == 4);
    CHECK(sp[0].kind == LEDGER_SPAN_MMIO && sp[1].kind == LEDGER_SPAN_MMIO);
    if (failure(14)) return 0;
    last_spans = n; return 1;
}
int paging_map_phys(u32 va, u32 pa, u32 n, u32 flags)
{
    ++map_calls;
    CHECK(last_spans == 2 && flags == (PAGE_RW | PTE_PCD));
    CHECK((va == MEM_TRIDENT_LFB_VA && pa == LFB_BASE && n == 1024) ||
          (va == MEM_TRIDENT_REGS_VA && pa == REG_BASE && n == 16));
    if ((failure(15) && map_calls == 1) || (failure(16) && map_calls == 2)) return -71;
    present_maps |= va == 0x20000000 ? 1u : 2u; return 0;
}
int paging_set_not_present(u32 first, u32 last)
{
    CHECK((first == 0x20000000 && last == 0x203fffff) ||
          (first == MEM_TRIDENT_REGS_VA && last == MEM_TRIDENT_REGS_VA + 65535));
    CHECK(!ledger_surfaces[2].npages && !ledger_surfaces[3].npages);
    present_maps &= first == 0x20000000 ? ~1u : ~2u; return 0;
}
int ledger_resource_set_map(u32 rid, u32 first, u32 end)
{
    CHECK(rid == 3 || rid == 4);
    if (end) {
        ++map_records;
        if ((failure(17) && map_records == 1) || (failure(18) && map_records == 2)) return 0;
        CHECK(first == (rid == 3 ? LFB_BASE / PAGE_SIZE : REG_BASE / PAGE_SIZE));
        CHECK(end == (rid == 3 ? LFB_BASE / PAGE_SIZE + 1024 : REG_BASE / PAGE_SIZE + 16));
    } else CHECK(!first);
    ledger_resources[rid].map_first = first; ledger_resources[rid].map_end = end; return 1;
}
int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 flags, u32 *pfn)
{
    CHECK(TRI_RAM_FALLBACK_DEFAULT && owner == LEDGER_OWNER_BOOT && !ram_live);
    CHECK(n > 0 && (u32)n == expected_capacity / PAGE_SIZE);
    CHECK(first == MEM_PHYS_EXEC_FLOOR / PAGE_SIZE && end == pgalloc_arena_end() && flags == LEDGER_TOP_DOWN);
    if (fail_ram_once) { fail_ram_once=false; return 0; }
    ++allocs; ram_live = true; *pfn = (u32)ram / PAGE_SIZE; return 1;
}
u32 pgalloc_arena_end(void) { return ((u32)ram + sizeof(ram)) / PAGE_SIZE; }
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    CHECK(owner == LEDGER_OWNER_BOOT && pfn == (u32)ram / PAGE_SIZE && n > 0 && ram_live);
    ++frees; ram_live = false; return 1;
}
static void check_geometry(const struct ledger_surface *s)
{
    CHECK(!irq_enabled && current_cr3 == 0x1000 && present_maps == 3);
    CHECK(s->width == expected_width && s->height == expected_height && s->pitch == expected_pitch);
    CHECK(s->npages == expected_capacity / PAGE_SIZE);
    if (TRI_RAM_FALLBACK_DEFAULT && s->role == LEDGER_ROLE_CLIENT) {
        CHECK(s->first == (u32)ram / PAGE_SIZE && s->owner == LEDGER_OWNER_BOOT);
        CHECK(s->backing == LEDGER_SB_RAM && s->cache == LEDGER_CACHE_WB);
    } else {
        CHECK(s->first == LFB_BASE / PAGE_SIZE && s->owner == 8);
        CHECK(s->backing == LEDGER_SB_VRAM && s->cache == LEDGER_CACHE_UC);
    }
    CHECK(s->backend == LEDGER_SF_TRIDENT && s->format == expected_format && s->planes == 1);
    CHECK(s->perm_max == LEDGER_PERM_RW && !s->lease_count && !s->closing);
}
int ledger_surface_create(const struct ledger_surface *s, u32 *sid)
{
    ++surface_calls;
    check_geometry(s);
    CHECK(s->role == ((!ledger_surfaces[2].npages) ? LEDGER_ROLE_DISPLAY : LEDGER_ROLE_CLIENT));
    if (fail_client_once && s->role == LEDGER_ROLE_CLIENT) { fail_client_once = false; return 0; }
    /* The real ledger only permits aliases with exactly equal extents/shapes. */
    for (u32 i = 2; i <= 3; ++i)
        if (ledger_surfaces[i].npages)
            CHECK(ledger_surfaces[i].npages == s->npages && ledger_surfaces[i].pitch == s->pitch);
    if ((failure(20) && surface_calls == 1) ||
        (failure(21) && surface_calls == 2)) return 0;
    *sid = ledger_surfaces[2].npages ? 3 : 2;
    if (wrong_sid_once) { *sid = 4; wrong_sid_once = false; }
    u32 gen = ledger_surfaces[*sid].gen + 1;
    ledger_surfaces[*sid] = *s;
    ledger_surfaces[*sid].gen = gen;
    gfx_surface_unready &= ~(1u << *sid);
    return 1;
}
int ledger_surface_regen(u32 sid, const struct ledger_surface *s)
{
    CHECK(sid == 2 || sid == 3);
    CHECK(revokes >= 2 && !ledger_surfaces[sid].lease_count);
    CHECK((gfx_surface_unready & 12) == 12);
    struct ledger_surface shaped = *s;
    shaped.role = ledger_surfaces[sid].role;
    check_geometry(&shaped);
    CHECK(s->first == ledger_surfaces[sid].first && s->npages == ledger_surfaces[sid].npages);
    ++regens;
    if ((failure(68) && sid == 2) || (failure(69) && sid == 3)) return 0;
    struct ledger_surface *old = &ledger_surfaces[sid];
    old->width = s->width; old->height = s->height; old->pitch = s->pitch;
    old->format = s->format; ++old->gen;
    return 1;
}
int ledger_surface_release(u32 sid)
{
    CHECK(sid == 2 || sid == 3 || sid == 4);
    struct ledger_surface *s = &ledger_surfaces[sid];
    CHECK(s->npages && !s->lease_count);
    if (s->backing == LEDGER_SB_RAM) CHECK(pgalloc_free_n_owner(s->owner, s->first, s->npages));
    else CHECK(s->backing == LEDGER_SB_VRAM);
    s->npages = 0; ++release_calls; return 1;
}
void *kmemset(void *p, int c, u32 n)
{
    u8 *s = p; for (u32 i = 0; i < n; ++i) s[i] = c; return p;
}
void kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr; CHECK(fmt[0] == '['); ++logs;
}
static void seed(void)
{
    CHECK(trident_fake_reset(&fake, fake_storage, sizeof(fake_storage), 0, 0) == 0);
    for (u32 d = 0; d < PCI_DEV_COUNT; ++d)
        for (u32 f = 0; f < PCI_FN_COUNT; ++f) config[d][f][0] = U32_MAX;
    const struct trident_pci_info *p = &fake.resources.pci;
    config[8][0][0] = p->vendor | ((u32)p->device << 16);
    config[8][0][1] = p->command;
    config[8][0][2] = p->revision;
    config[8][0][4] = p->bar0; config[8][0][5] = p->bar1;
    /* BAR2 is a placement fixture (TASK_TRIDENT_DRIVER §2-1), never mapped. */
    config[8][0][6] = 0x20800000;
    config[fxp_slot][0][0] = 0x12298086;
    config[fxp_slot][0][1] = 0x147;
    config[fxp_slot][0][4] = 0x20410008;
    config[fxp_slot][0][5] = 0x6001;
    config[fxp_slot][0][6] = 0x20500000;

}
/* Exercise the callback independently of boot context, as a live caller does. */
static void configure_cases(const struct trident_resources *r, struct tri_mode selected)
{
    selected = (struct tri_mode){.hd = 800, .vd = 600, .display_width = 832, .depth = 16, .bpp = 16};
    expected_width = 800; expected_height = 600; expected_pitch = 1664;
    expected_format = GFX_FMT_RGB565;
    u32 capacity = DISPLAY_CAPACITY;
    int expected = TRI_OK;
    switch (mode) {
    case 57:
        selected.depth = 24; selected.bpp = 32;
        expected_pitch = 3328; expected_format = GFX_FMT_XRGB8888; break;
    case 58: capacity -= PAGE_SIZE; expected = TRI_E_STATE; break;
    case 59: selected.depth = 15; selected.bpp = 16; expected = TRI_E_NOSYS; break;
    case 60: selected.depth = 24; selected.bpp = 24; expected = TRI_E_NOSYS; break;
    case 61: selected.hd = 0; expected = TRI_E_INVAL; break;
    case 62: selected.vd = 0; expected = TRI_E_INVAL; break;
    case 63: selected.display_width = 799; expected = TRI_E_INVAL; break;
    case 64: selected.display_width = 65535; expected = TRI_E_INVAL; break;
    case 65: selected.vd = 65535; expected = TRI_E_CAP_TOO_SMALL; break;
    case 66: /* Both leases must be revoked before either regen. */
    case 67:
        ledger_surfaces[2].lease_count = ledger_surfaces[3].lease_count = 1;
        if (mode == 67) expected = TRI_E_BUSY;
        break;
    case 68: case 69: expected = TRI_E_SURFACE; break;
    case 70: ledger_surfaces[3].gen = LEDGER_SURFACE_GEN_MAX; expected = TRI_E_SURFACE; break;
    case 71: ledger_surfaces[2].closing = 1; expected = TRI_E_SURFACE; break;
    case 72: kctx_irq_depth = 1; expected = TRI_E_STATE; break;
    case 73: kctx_exc_depth = 1; expected = TRI_E_STATE; break;
    case 74: expected = TRI_E_INVAL; break;
    }
    /* Public surfaces stay public after a successful switch. Failure hides both. */
    gfx_surface_unready &= ~12u;
    current_cr3 = 0x3000;
    u32 before = reloads;
    CHECK(r->configure(mode == 74 ? 0 : &selected, capacity, true) == expected);
    CHECK(current_cr3 == 0x3000 && irq_enabled);
    CHECK(surface_calls == 2 && allocs == TRI_RAM_FALLBACK_DEFAULT && !frees);
    if (expected == TRI_OK) {
        CHECK(reloads == before + 2 && regens == 2 && revokes == 2);
        CHECK((gfx_surface_unready & 12) == 0);
        CHECK(r->display_sid == 2 && r->client_sid == 3 && r->generation == 2);
        CHECK(r->client == (TRI_RAM_FALLBACK_DEFAULT ? ram : (u8 *)r->lfb) && r->client_bytes == expected_pitch * 600);
        CHECK(r->display.width == 800 && r->display.height == 600 && r->display.pitch == expected_pitch);
        CHECK(r->display.bytes == r->client_bytes && r->client_surface.bytes == r->client_bytes);
        CHECK(r->client_surface.pitch == expected_pitch && r->client_surface.width == 800);
        CHECK(r->display.cpp == (mode == 57 ? 4 : 2));
        CHECK(r->display.format == (mode == 57 ? TRI_FMT_XRGB8888 : TRI_FMT_RGB565));
        for (u32 sid = 2; sid <= 3; sid++) {
            const struct ledger_surface *sf = &ledger_surfaces[sid];
            CHECK(sf->first == ((TRI_RAM_FALLBACK_DEFAULT && sid == 3) ? (u32)ram / PAGE_SIZE : LFB_BASE / PAGE_SIZE) && sf->npages == DISPLAY_CAPACITY / PAGE_SIZE);
            CHECK(sf->gen == 2 && sf->width == 800 && sf->pitch == expected_pitch);
            CHECK(sf->role == (sid == 2 ? LEDGER_ROLE_DISPLAY : LEDGER_ROLE_CLIENT));
        }
        /* Return to INDEX8 without replacing backing or SIDs. */
        selected = (struct tri_mode){.hd = 640, .vd = 480, .display_width = 640, .depth = 8, .bpp = 8};
        expected_width = 640; expected_height = 480; expected_pitch = 640; expected_format = GFX_BB_PACKED8;
        CHECK(r->configure(&selected, DISPLAY_CAPACITY, true) == TRI_OK);
        CHECK(r->generation == 3 && r->client_bytes == 307200 && regens == 4);
    } else if (mode == 67 || mode == 68 || mode == 69) {
        CHECK(reloads == before + 2 && revokes == 2 && (gfx_surface_unready & 12) == 12);
        CHECK(r->generation == 1 && r->client_bytes == 307200);
    } else {
        CHECK(!regens && !revokes && r->generation == 1);
    }
}
/* Provisional -> final resize, retry, SID and private-before-final contracts. */
static void lifetime_cases(const struct trident_resources *r, struct tri_mode selected)
{
    u32 provisional = mode == 76 ? DISPLAY_CAPACITY : mode == 77 ? 1966080u : 307200u;
    expected_capacity = provisional;
    CHECK(r->configure(&selected, provisional, false) == TRI_OK);
    CHECK(surface_calls == 2 && r->generation == 1 && !fake.operations);
    CHECK(r->configure(&selected, provisional, false) == TRI_OK && surface_calls == 2 && !regens);
    expected_capacity = 1966080u;
    if (mode == 78) fail_client_once = true;
    if (mode == 82 && TRI_RAM_FALLBACK_DEFAULT) fail_ram_once=true;
    if (mode == 79) wrong_sid_once = true;
    if (mode == 80) {
        ledger_surfaces[3].lease_count = 1;
        CHECK(r->configure(&selected, expected_capacity, true) == TRI_E_BUSY);
        CHECK(!release_calls && !regens && !revokes);
        ledger_surfaces[3].lease_count = 0;
    }
    int rc = r->configure(&selected, expected_capacity, true);
    if (mode == 78 || mode == 79 || (mode == 82 && TRI_RAM_FALLBACK_DEFAULT)) {
        CHECK(rc == TRI_E_SURFACE && !ledger_surfaces[2].npages && !ledger_surfaces[3].npages);
        CHECK(!r->generation && !r->client && !r->client_bytes);
        CHECK(r->configure(&selected, expected_capacity, true) == TRI_OK);
    } else CHECK(rc == TRI_OK);
    CHECK(r->display_sid == 2 && r->client_sid == 3);
    CHECK(ledger_surfaces[2].npages == 480 && ledger_surfaces[3].npages == 480);
    CHECK((gfx_surface_unready & 12) == 12 && !fake.operations);
    CHECK(release_calls >= (mode == 77 ? 0u : 2u));
    CHECK(r->configure(&selected, expected_capacity - PAGE_SIZE, true) == TRI_E_STATE);
    /* false after final must retain capacity and reset default geometry. */
    selected.depth = selected.bpp = 16;
    expected_pitch = 1280; expected_format = GFX_FMT_RGB565;
    CHECK(r->configure(&selected, expected_capacity, true) == TRI_OK);
    selected.depth = selected.bpp = 8;
    expected_pitch = 640; expected_format = GFX_BB_PACKED8;
    CHECK(r->configure(&selected, 307200, false) == TRI_OK);
    CHECK(ledger_surfaces[3].npages == 480 && r->client_surface.cpp == 1);
    CHECK(trident_resources_rollback(false) == TRI_OK);
    CHECK(!present_maps && !ledger_surfaces[2].npages && !ledger_surfaces[3].npages && !ram_live && allocs==frees);
}
static void run(void)
{
    seed();
    if (mode == 55) config[8][0][4] = TRI_BAR0_PHYS + TRI_BAR0_DECODE_BYTES;
    if (mode == 3) config[8][0][0] ^= 1;
    if (mode == 4) config[8][0][1] = 0;
    if (mode == 5) config[8][0][2] = 0xd2;
    if (mode == 6) config[8][0][4] |= 1;
    if (mode == 7) config[8][0][5] |= 4;
    if (mode == 8) config[fxp_slot][0][6] = 0x20300000; /* full 1MiB overlaps */
    if (mode == 9) { /* Unknown enabled endpoint outside Trident does not veto startup. */
        config[10][0][0] = 0x99998888;
        config[10][0][1] = 2;
        config[10][0][4] = 0x30000000;
    }
    if (mode == 48 || mode == 50) {
        config[10][0][0] = 0x77776666; config[10][0][1] = 2;
        config[10][0][4] = mode == 48 ? 0x20001000 : 0x1ff00000;
        if (mode == 50) ledger_resources[0] = (struct ledger_resource){
            .raw_bar = 0x1ff00000, .decode_first = 0x1ff00, .decode_end = 0x20001,
            .bdf = 0x50, .vendor = 0x6666, .device = 0x7777, .bus = LEDGER_BUS_PCI,
            .bar = 0, .width_basis = LEDGER_WB_DATASHEET
        };
    }
    if (mode >= 51 && mode <= 54) {
        config[10][0][0] = 0x77776666; config[10][0][1] = 2;
        config[10][0][4] = mode == 53 ? 0x20001004 : mode == 54 ? 8 : 0x30000004;
        /* A 64bit upper dword is not another BAR. */
        config[10][0][5] = mode == 52 ? 0x20000000 : 0;
    }
    if (mode == 49) config[fxp_slot][0][0] = U32_MAX; /* no 82557 required */
    if (mode == 25) config[8][0][6] = 0x20000000;
    if (mode == 26) config[fxp_slot][0][4] = 0x20420008; /* CSR need not be adjacent */
    if (mode == 27) config[fxp_slot][0][12] = 1;
    if (mode == 28) config[8][0][12] = 1;
    if (mode == 29) {
        config[fxp_slot][0][3] = 0x10000;
        config[fxp_slot][0][8] = 0x20302000; /* forwarding 20000000..203FFFFF */
    }
    if (mode == 30) kctx_irq_depth = 1;
    if (mode == 31) kctx_exc_depth = 1;
    if (mode == 32) config[8][0][5] += 0x10000;
    if (mode == 34) config[fxp_slot][0][4] = 0x20000008;
    if (mode >= 37 && mode <= 40) {
        config[13][0][0] = 0x022110de;
        config[13][0][1] = 2;
        config[13][0][4] = mode == 38 ? 0x20000000 : 0x30000000;
        config[13][0][5] = 0x40000008;
        config[13][0][6] = 0x50000000;
        config[13][0][11] = mode == 39 ? 0 : 0x127010fc;
        if (mode == 40) config[13][0][5] = 0x40000000;
    }
    if (mode == 41 || mode == 42) {
        config[10][0][0] = 0x77776666; config[10][0][1] = 2;
        config[10][0][4] = 0x30000000;
        ledger_resources[0] = (struct ledger_resource){
            .raw_bar = 0x30000000, .decode_first = 0x30000, .decode_end = 0x30100,
            .bdf = 0x50, .vendor = 0x6666, .device = 0x7777, .bus = LEDGER_BUS_PCI,
            .bar = 0, .width_basis = mode == 41 ? LEDGER_WB_DATASHEET : LEDGER_WB_RAW
        };
    }
    if (mode >= 43 && mode <= 47) {
        config[10][0][0] = 0x56781234; config[10][0][1] = 2;
        config[10][0][3] = 0x10000; /* independent PCI bridge, no local BAR */
        config[10][0][8] = mode == 44 ? 0x20302000 : 0x30f03000;
        config[10][0][9] = mode == 45 ? 0x20312001 : mode == 46 ? 0x30f13001 : 0x0000fff0;
        if (mode == 46) config[10][0][10] = 1; /* 64bit forwarding above 4GiB */
        if (mode == 47) config[10][0][8] = 0xfff0f000; /* final page, no u32 wrap */
    }
    if (mode == 35 || mode == 36) {
        /* Same-slot multifunction endpoint, second function's full interval. */
        config[fxp_slot][0][3] = 0x800000;
        config[fxp_slot][1][0] = 0x12298086; config[fxp_slot][1][1] = 2;
        config[fxp_slot][1][4] = mode == 35 ? 0x20001008 : 0x20600008;
    }
    CHECK(!trident_resources_get());
    int rc = trident_boot_reserve();
    if (mode >= 56 || mode == 19 || mode == 20 || mode == 21 || mode == 5 || mode == 8 || mode == 25 || mode == 27 || mode == 28 || mode == 29 || mode == 34 || mode == 35 || mode == 38 || mode == 39 || mode == 40 || mode == 44 || mode == 45 || mode == 46 || mode == 48 || mode == 50 || mode == 53 || mode == 0 || mode == 51 || mode == 52 || mode == 54 || mode == 49 || mode == 9 || mode == 26 || mode == 42 || mode == 22 || mode == 23 || mode == 24 || mode == 36 || mode == 37 || mode == 41 || mode == 43 || mode == 47) {
        CHECK(rc == 0);
        const struct trident_resources *r = trident_resources_get();
        CHECK(r && r->ready && r->owner == 8 && r->bar0_rid == 3 && r->bar1_rid == 4);
        CHECK((u32)r->lfb == 0x20000000 && (u32)r->regs == MEM_TRIDENT_REGS_VA);
        CHECK(r->bar0_phys == LFB_BASE && r->bar1_phys == REG_BASE);
        CHECK(r->decode_bytes == 4194304 && r->mapped_bytes == 4194304 && r->regs_bytes == 65536);
        CHECK(!r->client && !r->client_bytes && r->ram_fallback == TRI_RAM_FALLBACK_DEFAULT);
        CHECK(r->configure && !surface_calls && !allocs && !frees);
        struct tri_mode selected = {.hd = 640, .vd = 480, .display_width = 640, .depth = 8, .bpp = 8};
        if (mode == 81) {
            fail_ram_once=TRI_RAM_FALLBACK_DEFAULT;
            CHECK(r->configure(&selected, DISPLAY_CAPACITY, true) ==
                  (TRI_RAM_FALLBACK_DEFAULT ? TRI_E_SURFACE : TRI_OK));
            if (TRI_RAM_FALLBACK_DEFAULT) {
                CHECK(!r->client && !ram_live && !ledger_surfaces[2].npages);
                CHECK(r->configure(&selected, DISPLAY_CAPACITY, true)==TRI_OK);
            }
            CHECK(trident_resources_rollback(false)==TRI_OK && !ram_live && allocs==frees);
            return;
        }
        if (mode >= 75) { lifetime_cases(r, selected); return; }
        int configured = r->configure(&selected, DISPLAY_CAPACITY, true);
        if (mode == 20 || mode == 21) {
            CHECK(configured == TRI_E_SURFACE && r->generation == 0);
            CHECK(release_calls == (mode == 21 ? 1u : 0u));
            CHECK(trident_resources_rollback(false) == 0 && !present_maps && !ram_live && allocs==frees);
            return;
        }
        CHECK(configured == TRI_OK && r->generation == 1);
        CHECK(r->display_sid == 2 && r->client_sid == 3);
        CHECK((gfx_surface_unready & 12) == 12);
        CHECK(ledger_surfaces[2].perm_max == LEDGER_PERM_RW);
        CHECK(ledger_surfaces[3].perm_max == LEDGER_PERM_RW);
        CHECK(r->pci.command == 2 && r->pci.revision == (mode == 5 ? 0xd2 : 0xd3));
        if (mode >= 56) {
            configure_cases(r, selected);
            return;
        }
        u32 reads = config_reads;
        CHECK(trident_boot_reserve() == 0 && config_reads == reads && map_calls == 2);
        if (mode == 22) {
            CHECK(trident_resources_rollback(true) == TRI_E_BUSY);
            CHECK(trident_resources_get() == r && present_maps == 3 && allocs == TRI_RAM_FALLBACK_DEFAULT && !release_calls);
            return;
        }
        if (mode == 23) {
            ledger_surfaces[3].lease_count = 1;
            CHECK(trident_resources_rollback(false) == TRI_E_BUSY);
            CHECK(trident_resources_get() == r && present_maps == 3 && allocs == TRI_RAM_FALLBACK_DEFAULT);
            return; /* API-level revoke failure leaves mappings and backing alive. */
        }
        if (mode == 24) ledger_surfaces[2].lease_count = ledger_surfaces[3].lease_count = 1;
        CHECK(trident_resources_rollback(false) == 0);
        CHECK(!trident_resources_get() && !present_maps && allocs == TRI_RAM_FALLBACK_DEFAULT && frees == allocs);
        CHECK(release_calls == 2 && last_spans == 2 && owner_calls == 1);
        CHECK(!ledger_resources[3].map_end && !ledger_resources[4].map_end);
        CHECK(ledger_resources[3].decode_end == LFB_BASE / PAGE_SIZE + 1024 && ledger_resources[4].decode_end == REG_BASE / PAGE_SIZE + 16);
        CHECK(trident_resources_rollback(false) == 0 && frees == allocs && release_calls == 2);
    } else {
        int expected = mode == 1 || mode == 2 || mode == 30 || mode == 31 ? TRI_E_STATE :
            (mode == 3 || mode == 4) ? TRI_E_DEVICE :
            mode == 15 || mode == 16 ? TRI_E_MAP : mode == 20 || mode == 21 ? TRI_E_SURFACE : TRI_E_RESOURCE;
        CHECK(rc == expected);
        CHECK(!trident_resources_get() && !present_maps && !allocs);
        if (mode >= 15 && mode <= 21) CHECK(last_spans == 2);
        if (mode == 21) CHECK(release_calls == 1);
        if (expected != TRI_E_STATE) CHECK(logs == 1);
    }
    CHECK(!fake.operations && !fake.pc98_initialized && !fake.resources.ready);
    if (mode != 1 && mode != 2 && mode != 30 && mode != 31)
        CHECK(read8_count && read16_count && read32_count);
}
void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(3), "b"(0), "c"(&mode), "d"(sizeof(mode)) : "memory");
    run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0) : "memory");
    __builtin_unreachable();
}
