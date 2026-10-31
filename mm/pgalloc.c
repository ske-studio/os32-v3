/* PFN allocator: eligibility and live ownership are deliberately separate.
 * No byte exclusive endpoint is used internally (including the final page).
 * Single CPU only; all state transitions preserve the caller's IF. */
#include "pgalloc.h"
#include "physmem.h"
#include "memmap.h"
#include "io.h"

/* The workspace bitmap is indexed by arena PFNs: both backing kinds keep the
 * workspace below PHYSMEM_LEGACY_MAX_PFN (ARENA_TOP: the low RAM tail;
 * FIXED: [MEM_LEDGER_META_BASE, MEM_LEDGER_META_END) in the kernel band).
 * There is no legacy allocator any more (T1a, TASK_T1_LEDGER §4-1): every
 * configuration boots through the model path. */
#define WORKSPACE_WORDS (BITMAP_WORDS(PHYSMEM_LEGACY_MAX_PFN))
static u32 *eligible, *bitmap;
static u32 limit_pfn, generic_end, total_pages, used_pages;
static int initialized;
static int online, model_mode;
static u32 workspace_first, workspace_end;
/* legacy arena end (PFN) after the model reserved its backing; frozen once. */
static u32 arena_end;
static u32 workspace_used[WORKSPACE_WORDS];
/* Private fixed/permanent provenance, NOT the caller's live physmem model.
 * Unknown apertures may be claimed, generic reserved/MMIO may not. */
static struct physmem device_boot_map;
/* L2: PFN ごとの owner 番号 (metadata の 2 bitmap の直後、1B/PFN)。 */
static u8 *owner_map;

/* 台帳の固定表 (BSS、§3-1)。資源と SURFACE は T1d / T1e が登録する。 */
struct ledger_owner ledger_owners[LEDGER_MAX_OWNERS];
struct ledger_region ledger_regions[LEDGER_MAX_REGIONS];
struct ledger_resource ledger_resources[LEDGER_MAX_RESOURCES];
struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
u32 ledger_region_count;
STATIC_ASSERT(sizeof(struct ledger_owner) == 24, ledger_owner_24);
STATIC_ASSERT(sizeof(struct ledger_region) == 16, ledger_region_16);
STATIC_ASSERT(sizeof(struct ledger_resource) == 32, ledger_resource_32);
STATIC_ASSERT(sizeof(struct ledger_surface) == 48, ledger_surface_48);
STATIC_ASSERT(LEDGER_MAX_OWNERS <= 256, ledger_owner_fits_u8);

/* R1 の観測 (§3-5)。深さは isr_stub.asm / setjmp.asm が書く。 */
volatile u32 kctx_irq_depth, kctx_exc_depth;
u32 ledger_irq_ops, ledger_exc_ops;
u32 ledger_irq_last[LEDGER_LAST_WORDS], ledger_exc_last[LEDGER_LAST_WORDS];
u32 ledger_bad_free, ledger_retire_refused, ledger_claim_refused;
u32 ledger_reclaim_pages, ledger_check_fail, ledger_res_overflow;
const char *ledger_check_tag;
/* ledger_reserve_set の一括 commit の通し番号 (区間の span_set)。 */
static u8 ledger_span_sets;

static const char ledger_fixed_names[LEDGER_OWNER_FIXED_LAST][LEDGER_NAME_LEN] = {
    "kernel", "boot", "bundle", "shlib", "gshell", "staging", "gfx"
};

static int bit(const u32 *map, u32 pfn)
{
    return (map[pfn / BITS_PER_U32] & (1UL << (pfn % BITS_PER_U32))) != 0;
}
int pgalloc_audit_ram(u32 pfn)
{
    if (!initialized) return 0;
    for (u32 i = 0; i < device_boot_map.count; i++) {
        const struct physmem_range *r = &device_boot_map.ranges[i];
        if (r->first <= pfn && pfn < r->end) return r->kind == PHYSMEM_RAM;
    }
    return 0;
}

static int bmp_test(u32 pfn) { return !bit(eligible, pfn) || bit(bitmap, pfn); }
static void bmp_set(u32 pfn) { bitmap[pfn / BITS_PER_U32] |= 1UL << (pfn % BITS_PER_U32); }
static void bmp_clear(u32 pfn) { bitmap[pfn / BITS_PER_U32] &= ~(1UL << (pfn % BITS_PER_U32)); }

/* Caller holds IRQ lock, owns backing, and has validated the model. */
static void init_core(const struct physmem *m, u32 *storage, u32 limit)
{
    u32 words, i, p;
    device_boot_map = *m;
    words = BITMAP_WORDS(limit);
    eligible = storage;
    bitmap = storage + words;
    owner_map = (u8 *)(storage + words * PGALLOC_L1_BITMAPS);
    limit_pfn = limit;
    total_pages = used_pages = 0;
    for (i = 0; i < words * PGALLOC_L1_BITMAPS; i++) storage[i] = 0;
    for (p = 0; p < limit; p++) owner_map[p] = 0;
    /* 固定の owner (§3-1)。kernel〜staging は PERSIST、gfx は DEVICE。 */
    for (i = 0; i < LEDGER_MAX_OWNERS; i++) {
        struct ledger_owner *o = &ledger_owners[i];
        u32 k;
        o->kind = 0; o->id = 0; o->pad = 0;
        o->pages = o->alloc_irq = o->free_irq = 0;
        for (k = 0; k < LEDGER_NAME_LEN; k++)
            o->name[k] = (i && i <= LEDGER_OWNER_FIXED_LAST) ?
                         ledger_fixed_names[i - 1][k] : 0;
        if (i && i <= LEDGER_OWNER_FIXED_LAST)
            o->kind = i == LEDGER_OWNER_GFX ? LEDGER_KIND_DEVICE : LEDGER_KIND_PERSIST;
    }
    ledger_region_count = 0;
    for (i = 0; i < m->count; i++) {
        if (m->ranges[i].kind != PHYSMEM_RAM) continue;
        for (p = m->ranges[i].first; p < m->ranges[i].end; p++) {
            eligible[p / BITS_PER_U32] |= 1UL << (p % BITS_PER_U32);
            total_pages++;
        }
    }
    initialized = 1;
}

/* Validate even manually constructed models at the trust boundary. */
static u32 model_limit(const struct physmem *m)
{
    u32 i, end, limit, allowed;
    const struct physmem_range *r;
    if (!m || !m->count || m->count > PHYSMEM_MAX_RANGES ||
        m->legacy_ceiling > PHYSMEM_LEGACY_MAX_PFN) return 0;
    allowed = PHYSMEM_SOURCE_LEGACY | PHYSMEM_SOURCE_MACHINE;
    allowed |= PHYSMEM_SOURCE_KERNEL_SLACK;
#if defined(PHYSMEM_HOST_TEST) && PHYSMEM_HOST_TEST == 1 && !defined(__KERNEL_BUILD__)
    allowed |= PHYSMEM_SOURCE_SYNTHETIC;
#endif
    end = limit = 0;
    for (i = 0; i < m->count; i++) {
        r = &m->ranges[i];
        if (r->first != end || r->first >= r->end ||
            r->end > PHYSMEM_MAX_PFN || r->kind > PHYSMEM_MMIO) return 0;
        if (r->kind == PHYSMEM_RAM) {
            if (!r->sources || (r->sources & ~allowed) ||
                ((r->sources & PHYSMEM_SOURCE_LEGACY) &&
                 r->end > PHYSMEM_LEGACY_MAX_PFN)) return 0;
            limit = r->end;
        } else if (r->sources) return 0;
        end = r->end;
    }
    return end == PHYSMEM_MAX_PFN ? limit : 0;
}
u32 pgalloc_metadata_bytes(const struct physmem *m)
{
    u32 limit, bytes;
    limit = model_limit(m);
    if (!limit) return 0;
    bytes = PGALLOC_META_BYTES(limit);
    return (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}
/* 全ページが L0 で RESERVED (RAM ではない) か。FIXED 型の backing の条件。 */
static int all_reserved(const struct physmem *m, u32 first, u32 end)
{
    u32 count;
    return physmem_count(m, first, end, PHYSMEM_RESERVED, &count) &&
           count == end - first;
}

/* backing の種類で検証経路を分ける (TASK_T1_LEDGER §3-3、B2):
 *   ARENA_TOP  低位 RAM の末尾。RAM から予約し、workspace はアプリ帯の最大
 *              上端より上 (2 枚 PDE のアプリが master の PT を USER で恒等
 *              写像できないこと)。metadata は exec のロード起点より上。
 *   FIXED      [MEM_LEDGER_META_BASE, MEM_LEDGER_META_END)。L0 で RESERVED
 *              (RAM ではない) なので予約はしない。PDE 0 (supervisor) の中なので
 *              アプリの恒等写像は届かない。
 * どちらも verify (実 PTE の検査) を通してから、失敗しうる処理の後に書く。 */
static int init_model(struct physmem *m, void *backing, u32 capacity,
                      u32 first, u32 ws_first, u32 ws_end, u32 kind,
                      int (*verify)(u32, u32, void *))
{
    struct physmem next;
    u32 bytes, pages, limit, addr, model_addr, lo, hi;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (initialized || !m || !backing || !verify) goto done;
    limit = model_limit(m);
    bytes = pgalloc_metadata_bytes(m);
    addr = (u32)backing;
    model_addr = (u32)m;
    if (!bytes || capacity < bytes || (addr & (PAGE_SIZE - 1)) ||
        addr > ~0UL - bytes || model_addr > ~0UL - sizeof(*m)) goto done;
    if (addr < model_addr + sizeof(*m) && model_addr < addr + bytes) goto done;
    pages = bytes / PAGE_SIZE;
    if (kind == PGALLOC_BACKING_FIXED) {
        lo = MEM_LEDGER_META_BASE / PAGE_SIZE;
        hi = MEM_LEDGER_META_END / PAGE_SIZE;
        if (first < lo || first >= hi || pages > hi - first ||
            !all_reserved(m, first, first + pages)) goto done;
    } else if (kind == PGALLOC_BACKING_ARENA_TOP) {
        if (first < MEM_PHYS_EXEC_FLOOR / PAGE_SIZE ||
            first >= PHYSMEM_LEGACY_MAX_PFN ||
            pages > PHYSMEM_LEGACY_MAX_PFN - first ||
            first + pages > physmem_legacy_end(m)) goto done;
    } else goto done;
#if !defined(PGALLOC_HOST_TEST) || PGALLOC_HOST_TEST != 1 || defined(__KERNEL_BUILD__)
    if (backing != P2V(first * PAGE_SIZE)) goto done;
#endif
    next = *m;
    /* Only the boot layout has workspace. Reserve DMA before metadata or any
     * general/BB allocation. A short RAM report is absence; overlap is failure.
     * Keep the original low arena bound for the later PERSIST freeze. */
    int dma = ws_first && m->legacy_ceiling >= (MEM_DMA_POOL_END + 1) / PAGE_SIZE;
    if (dma && !physmem_reserve_ram(&next, MEM_DMA_POOL_BASE / PAGE_SIZE,
                                    (MEM_DMA_POOL_END + 1) / PAGE_SIZE)) goto done;
    if ((kind == PGALLOC_BACKING_ARENA_TOP &&
         !physmem_reserve_ram(&next, first, first + pages)) ||
        !verify(first, pages, backing)) goto done;
    if (ws_first || ws_end) {
        if (ws_first >= ws_end ||
            (model_addr < ws_end * PAGE_SIZE &&
             ws_first * PAGE_SIZE < model_addr + sizeof(*m)) ||
            (addr < ws_end * PAGE_SIZE && ws_first * PAGE_SIZE < addr + bytes))
            goto done;
        if (kind == PGALLOC_BACKING_FIXED) {
            if (ws_first < MEM_LEDGER_META_BASE / PAGE_SIZE ||
                ws_end > MEM_LEDGER_META_END / PAGE_SIZE ||
                (ws_first < first + pages && first < ws_end) ||
                !all_reserved(&next, ws_first, ws_end)) goto done;
        } else if (ws_first < MEM_PHYS_WORKSPACE_FLOOR / PAGE_SIZE || ws_end > first ||
                   !physmem_reserve_ram(&next, ws_first, ws_end)) goto done;
        if (!verify(ws_first, ws_end - ws_first, P2V(ws_first * PAGE_SIZE)))
            goto done;
    }
    /* Never zero before all RAM reservations and mapping checks succeed. */
    /* DMA splits RAM eligibility, but ARENA_TOP placement still uses the
     * detected low tail. freeze later finds DMA's kernel PERSIST ownership. */
    generic_end = dma ? (kind == PGALLOC_BACKING_ARENA_TOP ? ws_first :
                        physmem_legacy_end(m)) : physmem_legacy_end(&next);
    *m = next;
    init_core(&next, (u32 *)backing, limit);
    if (dma) {
        for (u32 p = MEM_DMA_POOL_BASE / PAGE_SIZE;
             p < (MEM_DMA_POOL_END + 1) / PAGE_SIZE; p++) {
            owner_map[p] = LEDGER_OWNER_KERNEL;
            ledger_owners[LEDGER_OWNER_KERNEL].pages++;
        }
        /* Ineligible permanent pages cannot be freed or reclaimed as RAM. */
        if (!ledger_register_region(LEDGER_R_DMA, LEDGER_OWNER_KERNEL,
                                    MEM_DMA_POOL_BASE / PAGE_SIZE,
                                    (MEM_DMA_POOL_END + 1) / PAGE_SIZE,
                                    LEDGER_CACHE_WB, LEDGER_RF_PERMANENT)) goto done;
    }
    arena_end = generic_end;
    workspace_first = ws_first;
    workspace_end = ws_end;
    model_mode = 1;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int pgalloc_init_model(struct physmem *m, void *backing, u32 capacity,
                       u32 first, int (*verify)(u32, u32, void *))
{
    return init_model(m, backing, capacity, first, 0, 0,
                      PGALLOC_BACKING_ARENA_TOP, verify);
}

int pgalloc_init_layout(struct physmem *m, const struct pgalloc_layout *l,
                        int (*verify)(u32, u32, void *))
{
    if (!l || !l->workspace_first || !l->workspace_end) return 0;
    return init_model(m, l->metadata, l->capacity, l->metadata_first,
                      l->workspace_first, l->workspace_end, l->kind, verify);
}

u32 pgalloc_arena_end(void)
{
    return model_mode ? arena_end : 0;
}

int pgalloc_model_state(void)
{
    return model_mode ? (online ? PGALLOC_ONLINE : PGALLOC_BOOTSTRAP) : 0;
}

u32 pgalloc_alloc_pt(void)
{
    u32 p, result;
    unsigned int flags;
    flags = irq_save();
    result = 0;
    if (model_mode) {
        for (p = workspace_first; p < workspace_end; p++) {
            if (bit(workspace_used, p) ||
                !paging_verify_identity(p, 1, P2V(p * PAGE_SIZE))) continue;
            workspace_used[p / BITS_PER_U32] |= 1UL << (p % BITS_PER_U32);
            result = p * PAGE_SIZE;
            break;
        }
    }
    irq_restore(flags);
    return result;
}

void pgalloc_free_pt(u32 phys)
{
    u32 p;
    unsigned int flags;
    if (!model_mode) return;
    flags = irq_save();
    p = phys / PAGE_SIZE;
    if (!(phys & (PAGE_SIZE - 1)) && p >= workspace_first && p < workspace_end)
        workspace_used[p / BITS_PER_U32] &= ~(1UL << (p % BITS_PER_U32));
    irq_restore(flags);
}

/* Boot fail-stop contract: individual maps roll back, earlier successful
 * ranges may remain mapped on failure. No allocation is published then.
 * Iterate the frozen bitmap, never a caller-mutated model or byte end.
 * The split is paging_init's actual identity extent, not a RAM ceiling
 * (K6-RAM): below it every eligible page must ALREADY be identity mapped —
 * remapping there would silently undo boot protections — and above it each
 * eligible run is mapped now, taking new PTs from the boot workspace. */
int pgalloc_stage_online(void)
{
    u32 p, first, boot_end;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (!model_mode || online || !workspace_first || !paging_boot_context()) goto done;
    boot_end = paging_boot_identity_end();
    if (!boot_end) goto done;
    for (p = 0; p < limit_pfn && p < boot_end; p++)
        if (bit(eligible, p) &&
            !paging_verify_identity(p, 1, P2V(p * PAGE_SIZE))) goto done;
    p = boot_end;
    while (p < limit_pfn) {
        if (!bit(eligible, p)) { p++; continue; }
        first = p;
        do { p++; } while (p < limit_pfn && bit(eligible, p));
        if (paging_map_phys(first * PAGE_SIZE, first * PAGE_SIZE,
                            p - first, PAGE_RW) != 0 ||
            !paging_verify_identity(first, p - first, P2V(first * PAGE_SIZE)))
            goto done;
    }
    generic_end = limit_pfn;
    online = 1;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

#include "pgalloc_owner.inc"

#include "pgalloc_resource.inc"

#include "pgalloc_surface.inc"

/* 0 = 未凍結。凍結は ⑥ の 1 回だけ (§3-6)。
 * MEM_PHYS_EXEC_FLOOR から最初の PERSIST までの低位連続上端。
 * DMA/kernel の予約もここで止める。最高 RAM 番地や BB の下端とは限らない。 */
static u32 arena_top;

void ledger_arena_freeze(void)
{
    u32 p, o;
    unsigned int flags;
    flags = irq_save();
    for (p = MEM_PHYS_EXEC_FLOOR / PAGE_SIZE; !arena_top && p < arena_end; p++) {
        o = owner_map[p];
        if (o && ledger_owners[o].kind == LEDGER_KIND_PERSIST) arena_top = p;
    }
    if (!arena_top) arena_top = arena_end;
    irq_restore(flags);
}

u32 ledger_arena_top(void)
{
    return arena_top ? arena_top : pgalloc_arena_end();
}

int pgalloc_range_has_ram(u32 first, u32 end)
{
    u32 pages;
    unsigned int flags;
    int has;
    flags = irq_save();
    has = 1;
    if (initialized &&
        physmem_count(&device_boot_map, first, end, PHYSMEM_RAM, &pages))
        has = pages != 0;
    irq_restore(flags);
    return has;
}

u32 pgalloc_total_pages(void) { return total_pages; }
u32 pgalloc_free_pages(void) { return total_pages - used_pages; }
u32 pgalloc_limit_pfn(void) { return limit_pfn; }
