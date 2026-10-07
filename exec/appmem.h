/* T2f f2/f4: private kernel extent transactions. No PTE/PFN ownership here. */
#ifndef APPMEM_H
#define APPMEM_H

#include "types.h"

#include "appmem_types.h"

/* Small stack-local proposal: map range differs from merged extent range.
 * prepare leaves table and output unchanged on failure. No slot is reserved.
 * f3 must serialize prepare -> PTE staging -> publish, with no callbacks or
 * AS switch; publish accepts only a successful, unchanged-table proposal.
 * Output must not alias table/layout. Public wrappers use saved USER caller metadata. */
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

/* Private internal map entry. Outputs must not alias AS/table/layout.
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
/* Private internal public-policy wrapper. Internal EXEC_* entry is f9/f10. */
int appmem_unmap(struct addrspace *as, struct appmem_table *table, u32 base, u32 bytes);

/* Saved launch metadata and public-boundary translation. */
void appmem_init(struct addrspace *as, u32 img_end, u32 primary_end,
                 u32 exec_end, u32 guard_b);
int appmem_error_public(int error);

#endif
