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
#define WORKSPACE_WORDS ((PHYSMEM_LEGACY_MAX_PFN + 31) / 32)
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
u32 ledger_irq_last[3], ledger_exc_last[3];
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
    return (map[pfn / 32] & (1UL << (pfn % 32))) != 0;
}
static int bmp_test(u32 pfn) { return !bit(eligible, pfn) || bit(bitmap, pfn); }
static void bmp_set(u32 pfn) { bitmap[pfn / 32] |= 1UL << (pfn % 32); }
static void bmp_clear(u32 pfn) { bitmap[pfn / 32] &= ~(1UL << (pfn % 32)); }

/* Caller holds IRQ lock, owns backing, and has validated the model. */
static void init_core(const struct physmem *m, u32 *storage, u32 limit)
{
    u32 words, i, p;
    device_boot_map = *m;
    words = (limit + 31) / 32;
    eligible = storage;
    bitmap = storage + words;
    owner_map = (u8 *)(storage + words * 2);
    limit_pfn = limit;
    total_pages = used_pages = 0;
    for (i = 0; i < words * 2; i++) storage[i] = 0;
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
            eligible[p / 32] |= 1UL << (p % 32);
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
    /* No fallible work follows. Never zero before reservation/checks succeed. */
    *m = next;
    init_core(&next, (u32 *)backing, limit);
    generic_end = physmem_legacy_end(&next);
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
            workspace_used[p / 32] |= 1UL << (p % 32);
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
        workspace_used[p / 32] &= ~(1UL << (p % 32));
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

/* ======================================================================== */
/*  所有権台帳 (TASK_T1_LEDGER §3-1・§3-2、T1b)                              */
/*                                                                          */
/*  L2 の更新は必ず L1 と同じ IRQ 保存区間で、全部検査してから行う。         */
/* ======================================================================== */
static int owner_ok(u32 owner)
{
    return owner && owner < LEDGER_MAX_OWNERS && ledger_owners[owner].kind;
}
/* RAM を持てる owner (DEVICE は MMIO の予約だけ、X4)。 */
static int owner_ram(u32 owner)
{
    return owner_ok(owner) && ledger_owners[owner].kind != LEDGER_KIND_DEVICE;
}
static void take_page(u32 p, u32 owner) { bmp_set(p); owner_map[p] = (u8)owner; }
static void give_page(u32 p) { bmp_clear(p); owner_map[p] = 0; }
/* eligible かつ allocated で L2 が owner のページか。解放・移譲・回収・claim の
 * 全件検査が同じ述語を使う (noinline はカーネルの予算のため)。 */
static int __attribute__((noinline)) page_owned(u32 p, u32 owner)
{
    return bit(eligible, p) && bit(bitmap, p) && owner_map[p] == owner;
}
int pgalloc_page_owned(u32 pfn, u32 owner)
{
    return pfn < limit_pfn && owner_ok(owner) && page_owned(pfn, owner);
}

/* [pfn, pfn + n) が PFN の管理範囲 [0, limit_pfn) に収まるか (n > 0)。 */
static int __attribute__((noinline)) pfn_span_ok(u32 pfn, int n)
{
    return initialized && n > 0 && pfn < limit_pfn && (u32)n <= limit_pfn - pfn;
}

static void note_last(u32 *last, u32 op, u32 owner, void *eip)
{
    last[0] = op;
    last[1] = owner;
    last[2] = (u32)(uptr)eip;
}
/* R1 (§3-5、T2a): IRQ / 例外上の台帳操作を診断し、変更前に panic。
 * 失敗する要求も禁止。DMA プール内の割当はここを通らない (R4)。
 * 呼び手は IRQ 保存区間の中。 */
static void ledger_note(u32 op, u32 owner, void *eip)
{
    if (!kctx_irq_depth && !kctx_exc_depth) return;
    if (kctx_irq_depth) {
        ledger_irq_ops++;
        note_last(ledger_irq_last, op, owner, eip);
    }
    if (kctx_exc_depth) {
        ledger_exc_ops++;
        note_last(ledger_exc_last, op, owner, eip);
    }
    if (owner_ok(owner)) {
        if (op == LEDGER_OP_FREE || op == LEDGER_OP_RECLAIM)
            ledger_owners[owner].free_irq++;
        else
            ledger_owners[owner].alloc_irq++;
    }
    ledger_check_tag = "R1 context";
    for (;;) { _stop(); }
}
#define LEDGER_CALLER() __builtin_return_address(0)

int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner)
{
    struct ledger_owner *o;
    u32 i, k;
    unsigned int flags;
    int ok;
    if (!owner || (kind != LEDGER_KIND_AS && kind != LEDGER_KIND_MODULE &&
                   kind != LEDGER_KIND_DEVICE)) return 0;
    flags = irq_save();
    ok = 0;
    for (i = LEDGER_OWNER_FIXED_LAST + 1; i < LEDGER_MAX_OWNERS; i++)
        if (!ledger_owners[i].kind) break;
    if (initialized && i < LEDGER_MAX_OWNERS) {
        o = &ledger_owners[i];
        o->kind = (u8)kind;
        o->id = (u8)id;
        for (k = 0; k < LEDGER_NAME_LEN; k++) {
            o->name[k] = name ? *name : 0;
            if (name && *name) name++;
        }
        o->pages = o->alloc_irq = o->free_irq = 0;
        *owner = i;
        ok = 1;
    }
    irq_restore(flags);
    return ok;
}

int ledger_owner_retire(u32 owner)
{
    struct ledger_owner *o;
    u32 i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (!owner_ok(owner)) goto done;
    o = &ledger_owners[owner];
    if (o->kind == LEDGER_KIND_PERSIST || o->kind == LEDGER_KIND_DEVICE ||
        o->pages) goto refused;
    for (i = 0; i < ledger_region_count; i++)
        if (ledger_regions[i].owner == owner) goto refused;
    for (i = 0; i < LEDGER_MAX_SURFACES; i++)
        if (ledger_surfaces[i].npages && ledger_surfaces[i].owner == owner)
            goto refused;
    o->kind = 0;
    o->id = 0;
    for (i = 0; i < LEDGER_NAME_LEN; i++) o->name[i] = 0;
    o->alloc_irq = o->free_irq = 0;
    ok = 1;
    goto done;
refused:
    ledger_retire_refused++;
done:
    irq_restore(flags);
    return ok;
}

u32 ledger_owner_pages(u32 owner)
{
    return owner < LEDGER_MAX_OWNERS ? ledger_owners[owner].pages : 0;
}

/* 呼び手は IRQ 保存区間の中。[first, end) から n ページ連続の空きを探して
 * owner で取る。TOP_DOWN は上端から、BOTTOM_UP は下端からの最初適合。 */
static int alloc_owner(u32 owner, u32 n, u32 first, u32 end, u32 dir, u32 *pfn)
{
    u32 s, i, b;
    if (!online || !owner_ram(owner) || !pfn || !n || first >= end ||
        end > limit_pfn || n > end - first) return 0;
    if (dir == LEDGER_TOP_DOWN) {
        s = end - n;
        for (;;) {
            for (i = n; i > 0 && !bmp_test(s + i - 1); i--) {}
            if (!i) break;
            b = s + i - 1;          /* 候補の中で最も上の使用中ページ */
            if (b < first + n) return 0;
            s = b - n;
        }
    } else if (dir == LEDGER_BOTTOM_UP) {
        for (s = first;; s += i + 1) {
            if (s > end - n) return 0;
            for (i = 0; i < n && !bmp_test(s + i); i++) {}
            if (i == n) break;
        }
    } else return 0;
    for (i = 0; i < n; i++) take_page(s + i, owner);
    used_pages += n;
    ledger_owners[owner].pages += n;
    *pfn = s;
    return 1;
}

int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 dir,
                          u32 *pfn)
{
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_ALLOC, owner, LEDGER_CALLER());
    ok = n > 0 && alloc_owner(owner, (u32)n, first, end, dir, pfn);
    irq_restore(flags);
    return ok;
}

/* Clients dereference returned addresses: MODEL publishes the full eligible
 * limit only after stage_online has mapped it. */
u32 pgalloc_alloc_phys(u32 owner, int n)
{
    u32 pfn;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_ALLOC, owner, LEDGER_CALLER());
    ok = n > 0 && alloc_owner(owner, (u32)n, MEM_POOL_BASE / PAGE_SIZE,
                              generic_end, LEDGER_BOTTOM_UP, &pfn);
    irq_restore(flags);
    return ok ? pfn * PAGE_SIZE : 0;
}

int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    u32 i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_FREE, owner, LEDGER_CALLER());
    ok = 0;
    if (!owner_ok(owner) || !pfn_span_ok(pfn, n)) goto bad;
    for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
        const struct ledger_surface *sf = &ledger_surfaces[i];
        if (sf->npages && (!sf->closing || sf->lease_count) &&
            pfn < sf->first + sf->npages &&
            sf->first < pfn + (u32)n) goto bad;
    }
    for (i = 0; i < (u32)n; i++)
        if (!page_owned(pfn + i, owner)) goto bad;
    for (i = 0; i < (u32)n; i++) give_page(pfn + i);
    used_pages -= (u32)n;
    ledger_owners[owner].pages -= (u32)n;
    ok = 1;
    goto done;
bad:
    ledger_bad_free++;
done:
    irq_restore(flags);
    return ok;
}

int ledger_transfer(u32 pfn, int n, u32 from, u32 to)
{
    u32 i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_TRANSFER, from, LEDGER_CALLER());
    ok = 0;
    if (!owner_ok(from) || !owner_ram(to) || !pfn_span_ok(pfn, n)) goto done;
    for (i = 0; i < (u32)n; i++)
        if (!page_owned(pfn + i, from)) goto done;
    for (i = 0; i < (u32)n; i++) owner_map[pfn + i] = (u8)to;
    ledger_owners[from].pages -= (u32)n;
    ledger_owners[to].pages += (u32)n;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int ledger_reclaim_owner(u32 owner, u32 *pages)
{
    const struct ledger_surface *sf;
    u32 p, i, n, kind;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_RECLAIM, owner, LEDGER_CALLER());
    ok = 0;
    if (!initialized || !owner_ok(owner)) goto done;
    kind = ledger_owners[owner].kind;
    if (kind == LEDGER_KIND_DEVICE || kind == LEDGER_KIND_PERSIST) goto done;
    for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
        sf = &ledger_surfaces[i];
        if (sf->npages && sf->owner == owner) goto done;
    }
    /* 返し忘れが無ければ (pages == 0) 走査しない。AS / MODULE の L2 の
     * ページは全部 eligible かつ allocated。 */
    n = 0;
    for (p = 0; ledger_owners[owner].pages && p < limit_pfn; p++) {
        if (!page_owned(p, owner)) continue;
        give_page(p);
        n++;
        ledger_owners[owner].pages--;
    }
    used_pages -= n;
    ledger_reclaim_pages += n;
    if (pages) *pages = n;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int ledger_claim_fixed(u32 owner, u32 first, u32 end)
{
    u32 p;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_CLAIM, owner, LEDGER_CALLER());
    ok = 0;
    if (!initialized || !owner_ram(owner) || first >= end || end > limit_pfn)
        goto done;
    for (p = first; p < end; p++) {
        if (bit(eligible, p) && bit(bitmap, p) && owner_map[p] != owner) {
            ledger_claim_refused++;
            goto done;
        }
    }
    for (p = first; p < end; p++) {
        if (bit(eligible, p) && !bit(bitmap, p)) {
            take_page(p, owner);
            used_pages++;
            ledger_owners[owner].pages++;
        }
    }
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

/* 区間の表の末尾に 1 本足す (res_mask / span_set は 0)。呼び手は IRQ 保存区間
 * の中で検査を済ませ、空きがあることを確かめている。OUTSIDE は範囲から自動。 */
static struct ledger_region *region_put(u32 type, u32 owner, u32 first, u32 end,
                                        u32 cache, u32 rflags)
{
    struct ledger_region *r;
    r = &ledger_regions[ledger_region_count++];
    r->first = first;
    r->end = end;
    r->type = (u8)type;
    r->owner = (u8)owner;
    r->cache = (u8)cache;
    r->flags = (u8)(rflags | (end > limit_pfn ? LEDGER_RF_OUTSIDE : 0));
    r->res_mask = 0;
    r->span_set = 0;
    r->pad = 0;
    return r;
}

int ledger_register_region(u32 type, u32 owner, u32 first, u32 end, u32 cache,
                           u32 rflags)
{
    u32 i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (!initialized || !paging_boot_context() || !owner_ok(owner) ||
        type < LEDGER_R_FIXED || type > LEDGER_R_SURFACE_BACKING ||
        type == LEDGER_R_DEVICE || first >= end || end > PHYSMEM_MAX_PFN ||
        cache > LEDGER_CACHE_UC || (rflags & ~LEDGER_RF_PERMANENT) ||
        ledger_region_count >= LEDGER_MAX_REGIONS) goto done;
    for (i = 0; i < ledger_region_count; i++)
        if (first < ledger_regions[i].end && ledger_regions[i].first < end)
            goto done;
    region_put(type, owner, first, end, cache, rflags);
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int ledger_selfcheck(const char *tag)
{
    u32 counts[LEDGER_MAX_OWNERS];
    const struct ledger_region *r, *g;
    u32 p, i, j, o, live;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = initialized;
    for (i = 0; i < LEDGER_MAX_OWNERS; i++) counts[i] = 0;
    live = 0;
    for (p = 0; ok && p < limit_pfn; p++) {
        o = owner_map[p];
        if (bit(bitmap, p)) {
            live++;
            if (!bit(eligible, p) || !o) ok = 0;
        } else if (o && bit(eligible, p)) ok = 0;
        if (o) {
            if (!owner_ok(o)) ok = 0;
            counts[o]++;
        }
    }
    if (live != used_pages) ok = 0;
    for (i = 0; ok && i < LEDGER_MAX_OWNERS; i++)
        if (counts[i] != ledger_owners[i].pages) ok = 0;
    for (i = 0; ok && i < ledger_region_count; i++) {
        r = &ledger_regions[i];
        if (!owner_ok(r->owner) || r->first >= r->end) ok = 0;
        for (j = 0; j < i; j++) {
            g = &ledger_regions[j];
            /* 装置の予約は背景を細分してよい (§3-1 の BACKGROUND)。背景は
             * 起動時の登録 (重なりを断る) なので、重なる DEVICE より必ず前に
             * 並ぶ — 後ろの r が DEVICE、前の g が BACKGROUND の向きだけ許す。 */
            if (r->first < g->end && g->first < r->end &&
                !(r->type == LEDGER_R_DEVICE && g->type == LEDGER_R_BACKGROUND))
                ok = 0;
        }
    }
    for (i = 0; ok && i < LEDGER_MAX_SURFACES; i++)
        if (ledger_surfaces[i].npages && !owner_ok(ledger_surfaces[i].owner))
            ok = 0;
    if (!ok) {
        ledger_check_fail++;
        ledger_check_tag = tag;
    }
    irq_restore(flags);
    return ok;
}

/* ======================================================================== */
/*  MMIO 登録と検証済み資源レコード (§3-1・§3-2、T1d、D33・X4)               */
/*                                                                          */
/*  予約権限は資源レコード (decode 範囲 + その根拠) から来る。生の {first,   */
/*  end} だけでは予約できない。1 レコード = decode 区間 1 組 (B10)。         */
/* ======================================================================== */
int ledger_resource_add(const struct ledger_resource *rec, u32 *rid)
{
    u32 i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (!rec || !rid || !rec->bus || rec->bus > LEDGER_BUS_FIXED ||
        !rec->width_basis || rec->width_basis > LEDGER_WB_PROBE_UNVERIFIED ||
        rec->decode_first > rec->decode_end || rec->decode_end > PHYSMEM_MAX_PFN ||
        rec->map_first > rec->map_end ||
        (rec->map_first < rec->map_end &&
         (rec->map_first < rec->decode_first || rec->map_end > rec->decode_end)))
        goto done;
    for (i = 0; i < LEDGER_MAX_RESOURCES && ledger_resources[i].bus; i++) {}
    if (i == LEDGER_MAX_RESOURCES) {
        ledger_res_overflow++;
        goto done;
    }
    ledger_resources[i] = *rec;
    *rid = i;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int ledger_reserve_set(u32 owner, const struct ledger_span *spans, u32 n)
{
    struct ledger_span s[LEDGER_MAX_SPANS], t;
    const struct ledger_resource *rr;
    const struct ledger_region *g;
    struct ledger_region *r;
    u32 i, j, k, p, mine, hit;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_RESERVE, owner, LEDGER_CALLER());
    ok = 0;
    if (!online || !paging_boot_context() || !owner_ok(owner) ||
        ledger_owners[owner].kind != LEDGER_KIND_DEVICE || !spans || !n ||
        n > LEDGER_MAX_SPANS) goto done;
    /* (e) 正規化の前に span ごとに自分の資源レコードと照合する (B10)。空きの
     * レコードは width_basis 0 なのでここで落ちる。res は根拠のビットにして
     * 先頭の順に並べる。 */
    for (i = 0; i < n; i++) {
        t = spans[i];
        if (t.res >= LEDGER_MAX_RESOURCES) goto done;
        rr = &ledger_resources[t.res];
        if (!rr->width_basis || rr->width_basis > LEDGER_WB_GLUE_CONST ||
            t.first >= t.end || t.first < rr->decode_first ||
            t.end > rr->decode_end ||
            (t.kind != LEDGER_SPAN_MMIO && t.kind != LEDGER_SPAN_RAM)) goto done;
        /* 以後 kind は区間のキャッシュ属性で持つ (MMIO = UC、RAM = WB)。 */
        t.kind = t.kind == LEDGER_SPAN_MMIO ? LEDGER_CACHE_UC : LEDGER_CACHE_WB;
        t.res = 1UL << t.res;
        for (j = i; j && s[j - 1].first > t.first; j--) s[j] = s[j - 1];
        s[j] = t;
    }
    /* 正規化: 同種の重なる・接する span を併合 (根拠は和)、異種の重なりは拒否。 */
    k = 0;
    for (i = 0; i < n; i++) {
        if (k && s[i].first <= s[k - 1].end && s[i].kind == s[k - 1].kind) {
            if (s[i].end > s[k - 1].end) s[k - 1].end = s[i].end;
            s[k - 1].res |= s[i].res;
        } else {
            if (k && s[i].first < s[k - 1].end) goto done;
            s[k++] = s[i];
        }
    }
    n = k;
    /* 区間の表との照合 (§3-1 の拒否規則 (a)〜(c))。自分の DEVICE 区間は
     * 完全一致の判定に回す。 */
    mine = hit = 0;
    for (j = 0; j < ledger_region_count; j++) {
        g = &ledger_regions[j];
        if (g->type == LEDGER_R_DEVICE && g->owner == owner) {
            mine++;
            for (i = 0; i < n; i++)
                if (g->first == s[i].first && g->end == s[i].end &&
                    g->res_mask == s[i].res && g->cache == s[i].kind) hit++;
            continue;
        }
        if (g->type == LEDGER_R_BACKGROUND) continue;
        for (i = 0; i < n; i++)
            if (s[i].first < g->end && g->first < s[i].end) goto done;
    }
    /* 同 owner は同一集合だけ冪等 (何も変えない)、部分一致は拒否。 */
    if (mine) {
        ok = hit == n && mine == n;
        goto done;
    }
    if (n > LEDGER_MAX_REGIONS - ledger_region_count) goto done;
    /* (d) 管理範囲の中: L2 に owner があれば拒否 (使用中のページ — allocated
     * ⇔ owner ≠ 0 — と永久予約の両方)、RAM 種別 (WB) は全ページ eligible で
     * 管理範囲の内側。 */
    for (i = 0; i < n; i++) {
        if (s[i].kind == LEDGER_CACHE_WB && s[i].end > limit_pfn) goto done;
        for (p = s[i].first; p < s[i].end && p < limit_pfn; p++)
            if (owner_map[p] ||
                (s[i].kind == LEDGER_CACHE_WB && !bit(eligible, p))) goto done;
    }
    /* All fallible work is above this line. */
    ledger_span_sets++;
    for (i = 0; i < n; i++) {
        for (p = s[i].first; p < s[i].end && p < limit_pfn; p++) {
            if (bit(eligible, p)) {
                eligible[p / 32] &= ~(1UL << (p % 32));
                total_pages--;
            }
        }
        r = region_put(LEDGER_R_DEVICE, owner, s[i].first, s[i].end, s[i].kind,
                       LEDGER_RF_PERMANENT);
        r->res_mask = (u16)s[i].res;
        r->span_set = ledger_span_sets;
    }
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

/* ======================================================================== */
/*  SURFACE (§3-1・§3-8) と CPL=0 子のアリーナの上端 (§3-6)、T1e            */
/*                                                                          */
/*  T1 は型と表と登録・移譲だけ (lease の付け外しは T2、lease_count は 0)。  */
/*  登録・移譲は起動時と GUI の開始にしか走らないので cold (大きさで組ま   */
/*  せる、カーネルの予算 §4-5-R)。                                         */
/* ======================================================================== */
int ledger_surface_validate(const struct ledger_surface *sf)
{
    u32 p, i, bytes, plane_bytes;
    if (!sf || !owner_ok(sf->owner) || !sf->npages ||
        sf->first >= PHYSMEM_MAX_PFN || sf->npages > PHYSMEM_MAX_PFN - sf->first ||
        sf->npages > 0xffffffffUL / PAGE_SIZE ||
        !sf->backing || sf->backing > LEDGER_SB_MMIO ||
        !sf->planes || sf->planes > 4 || !sf->width || !sf->height || !sf->pitch ||
        sf->perm_max > LEDGER_PERM_RO) return 0;
    bytes = sf->npages * PAGE_SIZE;
    plane_bytes = (u32)sf->pitch * sf->height;
    for (i = 0; i < sf->planes; i++)
        if (sf->plane_offset[i] > bytes || plane_bytes > bytes - sf->plane_offset[i])
            return 0;
    if (sf->cache != (sf->backing <= LEDGER_SB_FIXED_RAM ?
                      LEDGER_CACHE_WB : LEDGER_CACHE_UC)) return 0;
    for (p = sf->first; p < sf->first + sf->npages; p++) {
        if (sf->backing == LEDGER_SB_RAM) {
            if (p >= limit_pfn || !page_owned(p, sf->owner)) return 0;
        } else {
            for (i = 0; i < ledger_region_count; i++) {
                const struct ledger_region *r = &ledger_regions[i];
                if (r->first <= p && p < r->end && r->cache == sf->cache &&
                    ((sf->backing == LEDGER_SB_FIXED_RAM &&
                      r->type == LEDGER_R_SURFACE_BACKING && r->owner == sf->owner) ||
                     (sf->backing == LEDGER_SB_VRAM && r->type == LEDGER_R_FIXED &&
                      r->owner == sf->owner) ||
                     (sf->backing >= LEDGER_SB_VRAM && r->type == LEDGER_R_DEVICE))) break;
            }
            if (i == ledger_region_count) return 0;
            if (sf->backing >= LEDGER_SB_VRAM && ledger_regions[i].type == LEDGER_R_DEVICE) {
                const struct ledger_region *r = &ledger_regions[i];
                for (i = 0; i < LEDGER_MAX_RESOURCES; i++)
                    if ((r->res_mask & (1U << i)) &&
                        ledger_resources[i].map_first <= p &&
                        p < ledger_resources[i].map_end) break;
                if (i == LEDGER_MAX_RESOURCES) return 0;
            }
        }
    }
    return 1;
}

u32 gfx_surface_unready;

int __attribute__((cold))
ledger_surface_create(const struct ledger_surface *sf, u32 *sid)
{
    u32 i, p, used_bytes;
    unsigned int flags = irq_save();
    int ok = 0;
    /* Padding uses the master identity alias until T2c. Reject before writes. */
    if (paging_current_cr3() != paging_kernel_pd_phys() ||
        kctx_irq_depth || kctx_exc_depth ||
        !ledger_surface_validate(sf) || sf->lease_count || sf->closing) goto done;
    for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
        const struct ledger_surface *other = &ledger_surfaces[i];
        if (other->npages && sf->first < other->first + other->npages &&
            other->first < sf->first + sf->npages) goto done;
    }
    for (i = 0; i < LEDGER_MAX_SURFACES; i++)
        if (!ledger_surfaces[i].npages && ledger_surfaces[i].gen != LEDGER_SURFACE_GEN_MAX) break;
    if (i == LEDGER_MAX_SURFACES) goto done;
    p = ledger_surfaces[i].gen + 1;
    ledger_surfaces[i] = *sf;
    ledger_surfaces[i].gen = p;
    gfx_surface_unready &= ~(1U << i);
    /* RAM padding belongs to this surface, and is never disclosed dirty. */
    if (sf->backing <= LEDGER_SB_FIXED_RAM) {
        used_bytes = (u32)sf->pitch * sf->height;
        for (p = 0; p < sf->npages * PAGE_SIZE; p++) {
            u32 n;
            for (n = 0; n < sf->planes; n++)
                if (p >= sf->plane_offset[n] && p - sf->plane_offset[n] < used_bytes) break;
            if (n == sf->planes) ((u8 *)P2V(sf->first * PAGE_SIZE))[p] = 0;
        }
    }
    if (sid) *sid = i;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

/* Preserve identity/backing and pixels; only validated geometry is replaceable. */
int ledger_surface_regen(u32 sid, const struct ledger_surface *geom)
{
    unsigned int flags = irq_save();
    struct ledger_surface next, *sf;
    int ok = 0;
    if (paging_current_cr3() != paging_kernel_pd_phys() ||
        kctx_irq_depth || kctx_exc_depth || sid >= LEDGER_MAX_SURFACES) goto done;
    sf = &ledger_surfaces[sid];
    if (!sf->npages || sf->lease_count || sf->closing || sf->gen == LEDGER_SURFACE_GEN_MAX)
        goto done;
    next = *sf;
    if (geom) {
        next.width = geom->width;
        next.height = geom->height;
        next.pitch = geom->pitch;
        next.format = geom->format;
        next.planes = geom->planes;
        for (u32 i = 0; i < 4; i++) next.plane_offset[i] = geom->plane_offset[i];
        if (!ledger_surface_validate(&next)) goto done;
    }
    next.gen++;
    *sf = next;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int ledger_surface_release(u32 sid)
{
    struct ledger_surface *sf;
    unsigned int flags = irq_save();
    int ok = 0;
    if (kctx_irq_depth || kctx_exc_depth ||
        sid >= LEDGER_MAX_SURFACES || !ledger_surfaces[sid].npages) goto done;
    sf = &ledger_surfaces[sid];
    sf->closing = 1;
    if (!sf->lease_count) {
        if (sf->backing == LEDGER_SB_RAM &&
            !pgalloc_free_n_owner(sf->owner, sf->first, (int)sf->npages)) goto done;
        sf->npages = 0;
    }
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

struct ledger_surface *ledger_surface_find(u32 backend, u32 role)
{
    u32 i;
    for (i = 0; i < LEDGER_MAX_SURFACES; i++)
        if (ledger_surfaces[i].npages && ledger_surfaces[i].backend == backend &&
            ledger_surfaces[i].role == role) return &ledger_surfaces[i];
    return 0;
}

int __attribute__((cold)) ledger_surface_transfer(u32 sid, u32 to)
{
    struct ledger_surface *sf;
    struct ledger_region *r;
    u32 i, from;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (sid >= LEDGER_MAX_SURFACES || !owner_ram(to) ||
        !ledger_surfaces[sid].npages) goto done;
    sf = &ledger_surfaces[sid];
    from = sf->owner;
    /* 2 回目以降 (GUI → CUI → GUI) は同じ owner なので何もしない (R5 (b))。
     * RAM の面はページの L2 ごと移す (全ページが from のものでなければ拒否)。 */
    ok = from == to || sf->backing != LEDGER_SB_RAM ||
         ledger_transfer(sf->first, (int)sf->npages, from, to);
    if (!ok || from == to) goto done;
    /* 固定 RAM の面は、それを含む同じ owner の SURFACE_BACKING 区間も移す。
     * MMIO の面は SURFACE の owner だけ (装置の DEVICE 区間は gfx のまま)。 */
    for (i = 0; sf->backing == LEDGER_SB_FIXED_RAM && i < ledger_region_count; i++) {
        r = &ledger_regions[i];
        if (r->type == LEDGER_R_SURFACE_BACKING && r->owner == from &&
            r->first <= sf->first && sf->first + sf->npages <= r->end)
            r->owner = (u8)to;
    }
    sf->owner = (u8)to;
done:
    irq_restore(flags);
    return ok;
}

/* 0 = 未凍結。凍結は ⑥ の 1 回だけ (§3-6)。 */
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
