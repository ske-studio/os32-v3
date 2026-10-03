/* T2f f2/f4: private, unlinked extent preparation. No PTE/PFN ownership here. */
#ifndef APPMEM_H
#define APPMEM_H

#include "types.h"

/* TASK_T2_APPBAND §3-1 / §6-1: 512 B of the 1,376 B AS budget. */
#define APPMEM_EXTENT_MAX 32
#define APPMEM_EINVAL (-1)
#define APPMEM_ENOVA  (-2)
#define APPMEM_EFULL  (-3)
#define APPMEM_ENOSPC (-4)

#define APPMEM_MAP_EXACT   1U
#define APPMEM_MAP_TOPDOWN 2U

enum appmem_kind {
    APPMEM_LIBC_INITIAL = 1,
    APPMEM_EXEC_INITIAL,
    APPMEM_ANON,
    APPMEM_EXEC_ARENA,
    APPMEM_EXEC_LARGE
};

struct appmem_extent { u32 base, end, kind, flags; };
/* flags is internal metadata, independent of map_flags. EXEC_LARGE callers
 * carry allocation identity here; even equal IDs never permit merging. */
struct appmem_table { struct appmem_extent e[APPMEM_EXTENT_MAX]; };
STATIC_ASSERT(sizeof(struct appmem_extent) == 16, appmem_extent_size);
STATIC_ASSERT(sizeof(struct appmem_table) == 512, appmem_table_size);

/* Image/exec initial heap/stack/guard/shlib are not table entries.
 * LIBC_INITIAL is an extent in f4 (registered by f5).
 * f2/f3 primary_mapped_end excludes the fixed primary heap. In f5, register
 * LIBC_INITIAL [page_align(img_end), old primary_mapped_end) and pass
 * primary_mapped_end = page_align(img_end), so returned holes are reusable.
 * img_end may be unaligned; the other boundaries must be page aligned.
 * f5 supplies these saved kernel values, never caller-provided metadata. */
struct appmem_layout {
    u32 img_end, primary_mapped_end, exec_heap_cur_end, guard_b;
};

/* Small stack-local proposal: map range differs from merged extent range.
 * prepare leaves table and output unchanged on failure. No slot is reserved.
 * f3 must serialize prepare -> PTE staging -> publish, with no callbacks or
 * AS switch; publish accepts only a successful, unchanged-table proposal.
 * Output must not alias table/layout. There is no public map API yet. */
struct appmem_plan {
    u32 base, end;
    struct appmem_extent merged;
    u32 first, remove_count, count;
};
int appmem_prepare(const struct appmem_table *table,
                   const struct appmem_layout *layout,
                   u32 bytes, u32 hint, u32 map_flags,
                   u32 kind, u32 extent_flags, struct appmem_plan *out);
int appmem_plan_valid(const struct appmem_table *table, const struct appmem_plan *plan);
void appmem_publish(struct appmem_table *table, const struct appmem_plan *plan);

/* Private host-only f3 entry. Outputs must not alias AS/table/layout.
 * The caller serializes this transaction: no callbacks/reentry/AS switches. */
struct addrspace;
int appmem_map(struct addrspace *as, struct appmem_table *table,
               const struct appmem_layout *layout, u32 bytes, u32 hint,
               u32 map_flags, u32 kind, u32 extent_flags, u32 *base_out);

#define APPMEM_KIND_MASK(kind) (1U << (kind))
#define APPMEM_PUBLIC_UNMAP_MASK (APPMEM_KIND_MASK(APPMEM_ANON) | APPMEM_KIND_MASK(APPMEM_LIBC_INITIAL))
/* Same proposal/revalidation contract as map. No mutation on prepare failure.
 * Remnants preserve kind/flags, including internal EXEC_LARGE identity. */
struct appmem_unmap_plan {
    u32 base, end, allowed_kind_mask, first, remove_count, count, remain_count;
    struct appmem_extent left, right;
};
int appmem_unmap_prepare(const struct appmem_table *table, u32 base, u32 end,
                         u32 allowed_kind_mask, struct appmem_unmap_plan *out);
int appmem_unmap_plan_valid(const struct appmem_table *table,
                            const struct appmem_unmap_plan *plan);
void appmem_unmap_publish(struct appmem_table *table, const struct appmem_unmap_plan *plan);
/* Private host-only public-policy wrapper. Internal EXEC_* entry is f9/f10. */
int appmem_unmap(struct addrspace *as, struct appmem_table *table, u32 base, u32 bytes);

#endif
