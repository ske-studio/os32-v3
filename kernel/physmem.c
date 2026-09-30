#include "physmem.h"
#include "memmap.h"

static int bounds(u32 first, u32 end)
{
    return first < end && end <= PHYSMEM_MAX_PFN;
}

void physmem_init(struct physmem *m)
{
    u32 i;
    m->count = 1;
    m->legacy_ceiling = 0;
    for (i = 0; i < PHYSMEM_MAX_RANGES; i++) {
        m->ranges[i].first = 0;
        m->ranges[i].end = 0;
        m->ranges[i].kind = PHYSMEM_UNKNOWN;
        m->ranges[i].sources = 0;
    }
    m->ranges[0].end = PHYSMEM_MAX_PFN;
}

static int append(struct physmem *m, u32 first, u32 end,
                  u32 kind, u32 sources)
{
    struct physmem_range *r;
    if (first == end) return 1;
    if (m->count) {
        r = &m->ranges[m->count - 1];
        if (r->end == first && r->kind == kind && r->sources == sources) {
            r->end = end;
            return 1;
        }
    }
    if (m->count == PHYSMEM_MAX_RANGES) return 0;
    r = &m->ranges[m->count++];
    r->first = first;
    r->end = end;
    r->kind = kind;
    r->sources = sources;
    return 1;
}

static int overlay(struct physmem *m, u32 first, u32 end,
                   u32 kind, u32 source)
{
    struct physmem next;
    const struct physmem_range *r;
    u32 i, lo, hi, merged_kind, merged_source;
    if (!bounds(first, end)) return 0;
    physmem_init(&next);
    next.count = 0;
    next.legacy_ceiling = m->legacy_ceiling;
    for (i = 0; i < m->count; i++) {
        r = &m->ranges[i];
        lo = first > r->first ? first : r->first;
        hi = end < r->end ? end : r->end;
        if (lo >= hi) {
            if (!append(&next, r->first, r->end, r->kind, r->sources)) return 0;
        } else {
            if (!append(&next, r->first, lo, r->kind, r->sources)) return 0;
            merged_kind = kind > r->kind ? kind : r->kind;
            merged_source = merged_kind == PHYSMEM_RAM ? r->sources | source : 0;
            if (!append(&next, lo, hi, merged_kind, merged_source)) return 0;
            if (!append(&next, hi, r->end, r->kind, r->sources)) return 0;
        }
    }
    *m = next;
    return 1;
}

int physmem_exclude(struct physmem *m, u32 first, u32 end, u32 kind)
{
    if (kind != PHYSMEM_RESERVED && kind != PHYSMEM_MMIO) return 0;
    return overlay(m, first, end, kind, 0);
}

int physmem_add_trusted(struct physmem *m, u32 first, u32 end, u32 source)
{
    if (source != PHYSMEM_SOURCE_MACHINE) {
#if defined(PHYSMEM_HOST_TEST) && PHYSMEM_HOST_TEST == 1 && !defined(__KERNEL_BUILD__)
        if (source != PHYSMEM_SOURCE_SYNTHETIC) return 0;
#else
        return 0;
#endif
    }
    return overlay(m, first, end, PHYSMEM_RAM, source);
}

void physmem_bootstrap_legacy(struct physmem *m, u32 mem_kb)
{
    u32 end, low;
    physmem_init(m);
    if (mem_kb > PHYSMEM_LEGACY_MAX_PFN * (PHYSMEM_PAGE_SIZE / 1024UL))
        mem_kb = PHYSMEM_LEGACY_MAX_PFN * (PHYSMEM_PAGE_SIZE / 1024UL);
    end = mem_kb / (PHYSMEM_PAGE_SIZE / 1024UL);
    low = MEM_APP_BAND_BASE / PHYSMEM_PAGE_SIZE;
    /* ホットデプロイ窓の撤去 (2026-09-09) で末尾の予約は無くなった。
     * legacy アリーナは実 RAM の末尾までそのまま使える。 */
    /* At most four normalized intervals; capacity cannot fail here. */
    physmem_exclude(m, 0, low, PHYSMEM_RESERVED);
    if (end > low) overlay(m, low, end, PHYSMEM_RAM, PHYSMEM_SOURCE_LEGACY);
    if (end > MEM_EXEC_LOAD_ADDR / PHYSMEM_PAGE_SIZE)
        m->legacy_ceiling = end;
}

u32 physmem_legacy_end(const struct physmem *m)
{
    u32 i, end;
    const struct physmem_range *r;
    end = MEM_EXEC_LOAD_ADDR / PHYSMEM_PAGE_SIZE;
    if (m->legacy_ceiling <= end) return 0;
    for (i = 0; i < m->count && end < m->legacy_ceiling; i++) {
        r = &m->ranges[i];
        if (r->end <= end) continue;
        if (r->kind != PHYSMEM_RAM || r->first > end) break;
        end = r->end < m->legacy_ceiling ? r->end : m->legacy_ceiling;
    }
    return end == MEM_EXEC_LOAD_ADDR / PHYSMEM_PAGE_SIZE ? 0 : end;
}

int physmem_find(const struct physmem *m, u32 first, u32 end,
                 u32 pages, u32 align_pages, u32 *pfn)
{
    u32 i, lo, hi, start, run_end, remainder;
    const struct physmem_range *r;
    if (!bounds(first, end) || !pages || pages > end - first ||
        !align_pages || align_pages > PHYSMEM_MAX_PFN ||
        (align_pages & (align_pages - 1))) return 0;
    start = end;
    run_end = PHYSMEM_MAX_PFN + 1;
    for (i = 0; i < m->count; i++) {
        r = &m->ranges[i];
        lo = first > r->first ? first : r->first;
        hi = end < r->end ? end : r->end;
        if (lo >= hi) continue;
        if (r->kind != PHYSMEM_RAM) {
            start = end;
            run_end = PHYSMEM_MAX_PFN + 1;
            continue;
        }
        if (run_end != lo) {
            start = lo;
            remainder = start & (align_pages - 1);
            if (remainder) start += align_pages - remainder;
        }
        run_end = hi;
        if (start < hi && pages <= hi - start) {
            *pfn = start;
            return 1;
        }
    }
    return 0;
}

int physmem_reserve_ram(struct physmem *m, u32 first, u32 end)
{
    u32 pages;
    if (!physmem_count(m, first, end, PHYSMEM_RAM, &pages) ||
        pages != end - first) return 0;
    return physmem_exclude(m, first, end, PHYSMEM_RESERVED);
}

int physmem_count(const struct physmem *m, u32 first, u32 end,
                  u32 kind, u32 *pages)
{
    u32 i, lo, hi, total;
    const struct physmem_range *r;
    if (!bounds(first, end) || kind > PHYSMEM_MMIO) return 0;
    total = 0;
    for (i = 0; i < m->count; i++) {
        r = &m->ranges[i];
        if (r->kind != kind) continue;
        lo = first > r->first ? first : r->first;
        hi = end < r->end ? end : r->end;
        if (lo < hi) total += hi - lo;
    }
    *pages = total;
    return 1;
}
