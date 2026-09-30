/* PFN allocator: eligibility and live ownership are deliberately separate.
 * No byte exclusive endpoint is used internally (including the final page).
 * Single CPU only; all state transitions preserve the caller's IF. */
#include "pgalloc.h"
#include "physmem.h"
#include "memmap.h"
#include "io.h"

/* Old boot has no metadata provider yet. Small, kernel-owned compatibility
 * backing only; it does not set the capacity of the model-based allocator.
 * PHYSMEM_LEGACY_MAX_PFN bounds the OLD LOADER's reportable extent and the
 * contiguous legacy arena, never total RAM (K6-RAM): the model path sizes its
 * metadata from the detected limit and its workspace bitmap stays indexed by
 * arena PFNs, which init_model keeps below PHYSMEM_LEGACY_MAX_PFN. */
#define LEGACY_WORDS ((PHYSMEM_LEGACY_MAX_PFN + 31) / 32)
static u32 legacy_metadata[LEGACY_WORDS * 2];
static u32 *eligible, *bitmap;
static u32 limit_pfn, generic_end, total_pages, used_pages;
static int initialized;
static int online, model_mode;
static u32 workspace_first, workspace_end;
static u32 workspace_used[LEGACY_WORDS];
/* Private fixed/permanent provenance, NOT the caller's live physmem model.
 * Unknown apertures may be claimed, generic reserved/MMIO may not. */
static struct physmem device_boot_map;

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
    limit_pfn = limit;
    total_pages = used_pages = 0;
    for (i = 0; i < words * 2; i++) storage[i] = 0;
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
    bytes = ((limit + 31) / 32) * sizeof(u32) * 2;
    return (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}
static int init_model(struct physmem *m, void *backing, u32 capacity,
                      u32 first, u32 ws_first, u32 ws_end,
                      int (*verify)(u32, u32, void *))
{
    struct physmem next;
    u32 bytes, pages, limit, addr, model_addr;
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
    if (first < MEM_EXEC_LOAD_ADDR / PAGE_SIZE ||
        first >= PHYSMEM_LEGACY_MAX_PFN ||
        pages > PHYSMEM_LEGACY_MAX_PFN - first ||
        first + pages > physmem_legacy_end(m)) goto done;
#if !defined(PGALLOC_HOST_TEST) || PGALLOC_HOST_TEST != 1 || defined(__KERNEL_BUILD__)
    if (addr != first * PAGE_SIZE) goto done;
#endif
    next = *m;
    if (!physmem_reserve_ram(&next, first, first + pages) ||
        !verify(first, pages, backing)) goto done;
    if (ws_first || ws_end) {
        if (ws_first < MEM_APP_BAND_MAX_TOP / PAGE_SIZE || ws_first >= ws_end ||
            ws_end > first ||
            (model_addr < ws_end * PAGE_SIZE &&
             ws_first * PAGE_SIZE < model_addr + sizeof(*m)) ||
            (addr < ws_end * PAGE_SIZE && ws_first * PAGE_SIZE < addr + bytes) ||
            !physmem_reserve_ram(&next, ws_first, ws_end) ||
            !verify(ws_first, ws_end - ws_first, (void *)(ws_first * PAGE_SIZE)))
            goto done;
    }
    /* No fallible work follows. Never zero before reservation/checks succeed. */
    *m = next;
    init_core(&next, (u32 *)backing, limit);
    generic_end = physmem_legacy_end(&next);
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
    return init_model(m, backing, capacity, first, 0, 0, verify);
}

int pgalloc_init_layout(struct physmem *m, const struct pgalloc_layout *l,
                        int (*verify)(u32, u32, void *))
{
    if (!l || !l->workspace_first || !l->workspace_end) return 0;
    return init_model(m, l->metadata, l->capacity, l->metadata_first,
                      l->workspace_first, l->workspace_end, verify);
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
    if (!model_mode) { pgalloc_free_page(phys); return; }
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

void pgalloc_init(u32 mem_kb)
{
    struct physmem m;
    unsigned int flags;
    u32 i, limit;
    flags = irq_save();
    if (!initialized) {
        physmem_bootstrap_legacy(&m, mem_kb);
        limit = PGALLOC_BASE / PAGE_SIZE;
        for (i = 0; i < m.count; i++)
            if (m.ranges[i].kind == PHYSMEM_RAM) limit = m.ranges[i].end;
        init_core(&m, legacy_metadata, limit);
        generic_end = limit;
        online = 1;
    }
    irq_restore(flags);
}

static int alloc_n_pfn(int n, u32 first, u32 end, u32 *pfn)
{
    u32 start, i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (!initialized || !pfn || n <= 0 || first >= end ||
        end > limit_pfn || (u32)n > end - first) goto done;
    for (start = first; start <= end - (u32)n; start++) {
        for (i = 0; i < (u32)n; i++) if (bmp_test(start + i)) break;
        if (i == (u32)n) {
            for (i = 0; i < (u32)n; i++) bmp_set(start + i);
            used_pages += (u32)n;
            *pfn = start;
            ok = 1;
            break;
        }
        start += i;
    }
done:
    irq_restore(flags);
    return ok;
}

int pgalloc_alloc_n_pfn(int n, u32 first, u32 end, u32 *pfn)
{
    if (!online) return 0;
    return alloc_n_pfn(n, first, end, pfn);
}

u32 pgalloc_alloc_n_range(int n, u32 lo, u32 hi)
{
    u32 pfn;
    if (lo < PGALLOC_BASE || lo >= hi ||
        (lo & (PAGE_SIZE - 1)) || (hi & (PAGE_SIZE - 1))) return 0;
    if (!pgalloc_alloc_n_pfn(n, lo / PAGE_SIZE, hi / PAGE_SIZE, &pfn)) return 0;
    return pfn * PAGE_SIZE;
}

/* Legacy clients dereference returned addresses: MODEL publishes the full
 * eligible limit only after mapping; old boot retains its low mapped limit. */
u32 pgalloc_alloc_n(int n)
{
    u32 pfn;
    if (!pgalloc_alloc_n_pfn(n, PGALLOC_BASE / PAGE_SIZE, generic_end, &pfn)) return 0;
    return pfn * PAGE_SIZE;
}
u32 pgalloc_alloc_page(void) { return pgalloc_alloc_n(1); }

int pgalloc_free_n_pfn(u32 first, int n)
{
    u32 i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (!initialized || n <= 0 || first >= limit_pfn ||
        (u32)n > limit_pfn - first) goto done;
    for (i = 0; i < (u32)n; i++)
        if (!bit(eligible, first + i) || !bit(bitmap, first + i)) goto done;
    for (i = 0; i < (u32)n; i++) bmp_clear(first + i);
    used_pages -= (u32)n;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}
void pgalloc_free_n(u32 phys, int n)
{
    if (phys & (PAGE_SIZE - 1)) return;
    (void)pgalloc_free_n_pfn(phys / PAGE_SIZE, n);
}
void pgalloc_free_page(u32 phys) { pgalloc_free_n(phys, 1); }

/* Permanent reservation. Reject live allocations atomically. Already
 * ineligible pages stay ineligible; this cannot manufacture RAM. */
int pgalloc_reserve_pfn(u32 first, u32 end)
{
    struct physmem next;
    u32 p;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (!initialized || first >= end || end > limit_pfn) goto done;
    for (p = first; p < end; p++) if (bit(bitmap, p)) goto done;
    next = device_boot_map;
    if (!physmem_exclude(&next, first, end, PHYSMEM_RESERVED)) goto done;
    device_boot_map = next;
    for (p = first; p < end; p++) {
        if (bit(eligible, p)) {
            eligible[p / 32] &= ~(1UL << (p % 32));
            total_pages--;
        }
    }
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}
/* Legacy fixed-address claim, not a permanent reservation. Existing live
 * pages are idempotent; ineligible pages are skipped, never resurrected. */
void pgalloc_mark_used(u32 phys, int n)
{
    u32 first, p, end;
    unsigned int flags;
    flags = irq_save();
    if (!initialized || n <= 0 || (phys & (PAGE_SIZE - 1))) goto done;
    first = phys / PAGE_SIZE;
    if (first >= limit_pfn || (u32)n > limit_pfn - first) goto done;
    end = first + (u32)n;
    for (p = first; p < end; p++) {
        if (bit(eligible, p) && !bit(bitmap, p)) {
            bmp_set(p);
            used_pages++;
        }
    }
done:
    irq_restore(flags);
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
