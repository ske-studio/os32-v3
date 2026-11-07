#ifndef OS32_LEGACY_LOWMAP_H
#define OS32_LEGACY_LOWMAP_H
#include "types.h"
/* Caller-owned storage, no allocation. One snapshot is the transaction token.
 * count/limit are fixed at open; restore never takes new caller geometry.
 * Restore writes all PTEs, both saved PDEs, flushes once, checks, then unlocks.
 * A lost token is diagnosed while the original snapshot still removes aliases.
 */
struct legacy_lowmap_snapshot {
    u32 root, master_pde, active_pde;
    u32 *saved_pte;
    u32 page_count, limit, restore_pte_bad, restore_pde_bad;
};
int paging_lowmap_busy(void);
int paging_lowmap_snapshot(struct legacy_lowmap_snapshot *s, u32 *saved_pte,
                          u32 page_count, u32 limit);
int paging_lowmap_restore(struct legacy_lowmap_snapshot *s);
int paging_lowmap_map_range(struct legacy_lowmap_snapshot *s,
                           u32 base, u32 end, u32 phys, u32 flags);
#endif
