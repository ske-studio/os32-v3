#include "appmem.h"
#include "memmap.h"
#include "paging.h"

#define APPMEM_U32_MAX (~(u32)0)

static int aligned(u32 value)
{
    return !(value & (PAGE_SIZE - 1));
}

static int table_count(const struct appmem_table *table)
{
    int count = 0, empty = 0;
    u32 end = MEM_EXEC_LOAD_ADDR;
    for (int i = 0; i < APPMEM_EXTENT_MAX; i++) {
        const struct appmem_extent *e = &table->e[i];
        if (!e->base && !e->end) {
            if (e->kind || e->flags) return APPMEM_EINVAL;
            empty = 1;
            continue;
        }
        if (empty || !aligned(e->base) || !aligned(e->end) ||
            e->base < end || e->base >= e->end ||
            e->end > MEM_APP_BAND_MAX_TOP ||
            e->kind < APPMEM_LIBC_INITIAL || e->kind > APPMEM_EXEC_LARGE)
            return APPMEM_EINVAL;
        end = e->end;
        count++;
    }
    return count;
}

static int range_free(const struct appmem_table *table, int count,
                      u32 base, u32 end)
{
    for (int i = 0; i < count; i++)
        if (base < table->e[i].end && end > table->e[i].base) return 0;
    return 1;
}

/* Both policies use this same descending search, clipped to their window. */
static u32 find_hole(const struct appmem_table *table, int count,
                     u32 low, u32 high, u32 size)
{
    for (int i = count - 1; i >= 0; i--) {
        const struct appmem_extent *e = &table->e[i];
        if (e->base >= high || e->end <= low) continue;
        if (e->end <= high && high - e->end >= size) return high - size;
        high = e->base > low ? e->base : low;
    }
    return high - low >= size ? high - size : 0;
}

static int mergeable(const struct appmem_extent *a,
                     const struct appmem_extent *b)
{
    return a->kind == b->kind && a->flags == b->flags &&
           a->kind != APPMEM_EXEC_LARGE;
}

int appmem_prepare(const struct appmem_table *table,
                   const struct appmem_layout *layout,
                   u32 bytes, u32 hint, u32 map_flags,
                   u32 kind, u32 extent_flags, struct appmem_plan *out)
{
    struct appmem_plan plan;
    u32 image, size, base = 0, end;
    int count;
    if (!table || !layout || !out || !bytes ||
        bytes > APPMEM_U32_MAX - (PAGE_SIZE - 1) ||
        (map_flags & ~(APPMEM_MAP_EXACT | APPMEM_MAP_TOPDOWN)) ||
        kind < APPMEM_LIBC_INITIAL || kind > APPMEM_EXEC_LARGE ||
        ((map_flags & APPMEM_MAP_EXACT) && !hint)) return APPMEM_EINVAL;
    if (layout->img_end < MEM_EXEC_LOAD_ADDR ||
        layout->img_end > APPMEM_U32_MAX - (PAGE_SIZE - 1)) return APPMEM_EINVAL;
    image = PAGE_ALIGN_UP(layout->img_end);
    if (!aligned(layout->primary_mapped_end) ||
        !aligned(layout->exec_heap_cur_end) || !aligned(layout->guard_b) ||
        image > layout->primary_mapped_end ||
        layout->primary_mapped_end > MEM_EXEC_HEAP_BASE ||
        layout->exec_heap_cur_end < MEM_EXEC_HEAP_BASE ||
        layout->exec_heap_cur_end > layout->guard_b ||
        layout->guard_b > MEM_APP_STACK_TOP - MEM_GUARD_SIZE)
        return APPMEM_EINVAL;
    size = PAGE_ALIGN_UP(bytes);
    if (hint && (!aligned(hint) || hint < MEM_EXEC_LOAD_ADDR ||
                 hint >= MEM_APP_BAND_MAX_TOP ||
                 size > APPMEM_U32_MAX - hint)) return APPMEM_EINVAL;
    count = table_count(table);
    if (count < 0) return count;
    if (hint) {
        end = hint + size;
        if (((hint >= layout->primary_mapped_end && end <= MEM_EXEC_HEAP_BASE) ||
             (hint >= layout->exec_heap_cur_end && end <= layout->guard_b)) &&
            range_free(table, count, hint, end)) base = hint;
    }
    if (!base && (map_flags & APPMEM_MAP_EXACT)) return APPMEM_ENOVA;
    if (!base) {
        if (map_flags & APPMEM_MAP_TOPDOWN)
            base = find_hole(table, count, layout->exec_heap_cur_end, layout->guard_b, size);
        else
            base = find_hole(table, count, layout->primary_mapped_end, MEM_EXEC_HEAP_BASE, size);
    }
    if (!base) return APPMEM_ENOVA;
    plan.base = base;
    plan.end = base + size;
    plan.merged = (struct appmem_extent){base, plan.end, kind, extent_flags};
    plan.first = 0;
    while (plan.first < (u32)count && table->e[plan.first].base < base) plan.first++;
    end = plan.first;
    while (plan.first && table->e[plan.first - 1].end == plan.merged.base &&
           mergeable(&table->e[plan.first - 1], &plan.merged)) {
        plan.first--;
        plan.merged.base = table->e[plan.first].base;
    }
    while (end < (u32)count && table->e[end].base == plan.merged.end &&
           mergeable(&table->e[end], &plan.merged)) {
        plan.merged.end = table->e[end].end;
        end++;
    }
    plan.remove_count = end - plan.first;
    plan.count = (u32)count;
    if (plan.count + 1 - plan.remove_count > APPMEM_EXTENT_MAX) return APPMEM_EFULL;
    *out = plan;
    return 0;
}

void appmem_publish(struct appmem_table *table, const struct appmem_plan *plan)
{
    u32 after = plan->count + 1 - plan->remove_count;
    if (!plan->remove_count) {
        for (u32 i = plan->count; i > plan->first; i--) table->e[i] = table->e[i - 1];
    } else {
        for (u32 i = plan->first + 1; i < after; i++)
            table->e[i] = table->e[i + plan->remove_count - 1];
    }
    table->e[plan->first] = plan->merged;
    for (u32 i = after; i < APPMEM_EXTENT_MAX; i++)
        table->e[i] = (struct appmem_extent){0, 0, 0, 0};
}
