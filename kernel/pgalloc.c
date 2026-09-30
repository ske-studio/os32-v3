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
STATIC_ASSERT(sizeof(struct ledger_surface) == 24, ledger_surface_24);
STATIC_ASSERT(LEDGER_MAX_OWNERS <= 256, ledger_owner_fits_u8);

/* R1 の観測 (§3-5)。深さは isr_stub.asm / setjmp.asm が書く。 */
volatile u32 kctx_irq_depth, kctx_exc_depth;
u32 ledger_irq_ops, ledger_exc_ops;
u32 ledger_irq_last[3], ledger_exc_last[3];
u32 ledger_bad_free, ledger_retire_refused, ledger_claim_refused;
u32 ledger_reclaim_pages, ledger_check_fail;
const char *ledger_check_tag;

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
        if (first < MEM_EXEC_LOAD_ADDR / PAGE_SIZE ||
            first >= PHYSMEM_LEGACY_MAX_PFN ||
            pages > PHYSMEM_LEGACY_MAX_PFN - first ||
            first + pages > physmem_legacy_end(m)) goto done;
    } else goto done;
#if !defined(PGALLOC_HOST_TEST) || PGALLOC_HOST_TEST != 1 || defined(__KERNEL_BUILD__)
    if (addr != first * PAGE_SIZE) goto done;
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
        } else if (ws_first < MEM_APP_BAND_MAX_TOP / PAGE_SIZE || ws_end > first ||
                   !physmem_reserve_ram(&next, ws_first, ws_end)) goto done;
        if (!verify(ws_first, ws_end - ws_first, (void *)(ws_first * PAGE_SIZE)))
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
                !paging_verify_identity(p, 1, (void *)(p * PAGE_SIZE))) continue;
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
            !paging_verify_identity(p, 1, (void *)(p * PAGE_SIZE))) goto done;
    p = boot_end;
    while (p < limit_pfn) {
        if (!bit(eligible, p)) { p++; continue; }
        first = p;
        do { p++; } while (p < limit_pfn && bit(eligible, p));
        if (paging_map_phys(first * PAGE_SIZE, first * PAGE_SIZE,
                            p - first, PAGE_RW) != 0 ||
            !paging_verify_identity(first, p - first, (void *)(first * PAGE_SIZE)))
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
/* R1 (§3-5): 割り込み / 例外フレームの上で走った台帳操作を数える。失敗した
 * 操作でも数える (診断)。DMA プールの内側の割当はここを通らない (R4)。
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
        if (sf->npages && sf->owner == owner &&
            (sf->lease_count || kind == LEDGER_KIND_AS)) goto done;
    }
    /* 返し忘れが無ければ (pages == 0) 走査しない。AS / MODULE の L2 は
     * 永久予約を持たない (pgalloc_reserve_pfn は PERSIST だけ) ので、
     * この番号のページは全部 eligible かつ allocated。 */
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

/* Permanent reservation. Reject live allocations and any page that already
 * carries an owner atomically. Already ineligible pages stay ineligible (and
 * ownerless); this cannot manufacture RAM. */
int pgalloc_reserve_pfn(u32 owner, u32 first, u32 end)
{
    struct physmem next;
    u32 p;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_RESERVE, owner, LEDGER_CALLER());
    ok = 0;
    if (!initialized || first >= end || end > limit_pfn || !owner_ok(owner) ||
        ledger_owners[owner].kind != LEDGER_KIND_PERSIST) goto done;
    for (p = first; p < end; p++) if (bit(bitmap, p) || owner_map[p]) goto done;
    next = device_boot_map;
    if (!physmem_exclude(&next, first, end, PHYSMEM_RESERVED)) goto done;
    device_boot_map = next;
    for (p = first; p < end; p++) {
        if (bit(eligible, p)) {
            eligible[p / 32] &= ~(1UL << (p % 32));
            total_pages--;
            owner_map[p] = (u8)owner;
            ledger_owners[owner].pages++;
        }
    }
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int ledger_register_region(u32 type, u32 owner, u32 first, u32 end, u32 cache,
                           u32 rflags)
{
    struct ledger_region *r;
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
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int ledger_selfcheck(const char *tag)
{
    u32 counts[LEDGER_MAX_OWNERS];
    const struct ledger_region *r;
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
        for (j = 0; j < i; j++)
            if (r->first < ledger_regions[j].end &&
                ledger_regions[j].first < r->end) ok = 0;
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

struct device_claim { u32 owner; struct sys_device_span span; };
static struct device_claim device_ledger[SYS_DEVICE_MAX_SPANS];
static u32 device_claims;

int pgalloc_device_reserve(u32 owner, const struct sys_device_span *spans,
                           u32 count, const struct sys_device_capability *cap,
                           u32 fixed_end)
{
    u32 i, j, matches, p, n;
    struct sys_device_span sorted[SYS_DEVICE_MAX_SPANS], tmp;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (!initialized || !online || !owner || !spans || !cap ||
        !(cap->flags & SYS_DEVICE_IDLE) || !count ||
        count > SYS_DEVICE_MAX_SPANS) goto done;
    /* Snapshot and normalize the complete input before inspecting ownership. */
    for (i = 0; i < count; i++) {
        tmp = spans[i];
        if (tmp.first >= tmp.end || tmp.end > PHYSMEM_MAX_PFN ||
            (tmp.kind != SYS_DEVICE_MMIO && tmp.kind != SYS_DEVICE_RAM)) goto done;
        if (tmp.kind == SYS_DEVICE_RAM &&
            (!(cap->flags & SYS_DEVICE_RAM_MAPPED) ||
             cap->mapped_first >= cap->mapped_end ||
             cap->mapped_end > PHYSMEM_MAX_PFN ||
             tmp.first < cap->mapped_first || tmp.end > cap->mapped_end)) goto done;
        j = i;
        while (j && sorted[j - 1].first > tmp.first) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = tmp;
    }
    n = 0;
    for (i = 0; i < count; i++) {
        if (n && sorted[i].first <= sorted[n - 1].end &&
            sorted[i].kind == sorted[n - 1].kind) {
            if (sorted[i].end > sorted[n - 1].end) sorted[n - 1].end = sorted[i].end;
        } else {
            if (n && sorted[i].first < sorted[n - 1].end) goto done;
            sorted[n++] = sorted[i];
        }
    }
    spans = sorted;
    count = n;
    matches = 0;
    for (i = 0; i < count; i++) {
        for (j = 0; j < device_claims; j++) {
            if (device_ledger[j].owner == owner) {
                if (device_ledger[j].span.first == spans[i].first &&
                    device_ledger[j].span.end == spans[i].end &&
                    device_ledger[j].span.kind == spans[i].kind) matches++;
            } else if (spans[i].first < device_ledger[j].span.end &&
                       device_ledger[j].span.first < spans[i].end) goto done;
        }
    }
    j = 0;
    for (i = 0; i < device_claims; i++) if (device_ledger[i].owner == owner) j++;
    if (j) { ok = matches == count && j == count; goto done; }
    if (count > SYS_DEVICE_MAX_SPANS - device_claims) goto done;
    for (i = 0; i < count; i++) {
        /* Retain the whole original legacy arena, including the dynamic A/B
         * hole and any boot top carve-out. Never shrink it for a device. */
        if (spans[i].first < MEM_EXEC_LOAD_ADDR / PAGE_SIZE ||
            spans[i].first < device_boot_map.legacy_ceiling ||
            spans[i].first < fixed_end) goto done;
        for (j = 0; j < device_boot_map.count; j++) {
            const struct physmem_range *r;
            r = &device_boot_map.ranges[j];
            if (spans[i].first >= r->end || r->first >= spans[i].end) continue;
            if (r->kind == PHYSMEM_RESERVED || r->kind == PHYSMEM_MMIO) goto done;
            if (r->kind == PHYSMEM_RAM) {
                u32 lo, hi;
                lo = spans[i].first > r->first ? spans[i].first : r->first;
                hi = spans[i].end < r->end ? spans[i].end : r->end;
                for (p = lo; p < hi; p++) if (!bit(eligible, p)) goto done;
            }
        }
        if (spans[i].kind == SYS_DEVICE_RAM) {
            if (spans[i].end > limit_pfn) goto done;
            for (p = spans[i].first; p < spans[i].end; p++)
                if (!bit(eligible, p)) goto done;
        }
        for (p = spans[i].first; p < spans[i].end && p < limit_pfn; p++)
            if (bit(bitmap, p)) goto done;
    }
    /* All fallible work is above this line. */
    for (i = 0; i < count; i++) {
        for (p = spans[i].first; p < spans[i].end && p < limit_pfn; p++) {
            if (bit(eligible, p)) {
                eligible[p / 32] &= ~(1UL << (p % 32));
                total_pages--;
            }
        }
        device_ledger[device_claims].owner = owner;
        device_ledger[device_claims++].span = spans[i];
    }
    ok = 1;
done:
    irq_restore(flags);
    return ok;
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
