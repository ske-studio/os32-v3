/* Shared F3/F4/F5 VRAM window and reservation declarations.
 * Canonical contract: GF_FEAT3_DESIGN.md, A0 addendum sections 1-2.
 * Foreground only: no IRQ/reentry or registry mutation during enumeration.
 * V0 implements these APIs and includes accel.h for GF3 error values. */
#ifndef HWDEV_GF_VRAM_VIEW_H
#define HWDEV_GF_VRAM_VIEW_H
#include <stdbool.h>
#include "command.h"

/* Caller-owned, initially all zero, immovable from map to successful unmap.
 * extent/map_base/map_bytes describe the page-rounded UC map; va/bytes
 * describe only the requested byte range. Borrow ops/owner until unmap.
 * BO-specific mapping model: nouveau (c03c1a1), drivers/gpu/drm/nouveau/
 * nouveau_bo.c:669-689; OS32 uses map_ops instead of BO/TTM. */
struct gf_vram_view {
    struct recover_bar_extent extent;
    volatile u8 *va;
    volatile u8 *map_base;
    u32 map_bytes;
    u32 bytes;
    bool mapped;
    const struct gf_instmem_map_ops *map_ops;
    void *map_owner;
};
/* off/len: nonzero length, both 4B aligned, contained in one live reservation
 * of ch. Page-rounded extent must fit BAR1/physical range and must not
 * overlap the existing boot display map (borrow that map for display I/O).
 * Never changes ch->va/mapped/push_extent. Caller ensures GPU completion,
 * accesses only va[0..bytes), drains posted writes and unmaps before release
 * or stop/recover. Persistent windows use page-aligned reservations.
 * Success alone publishes view and *out. Invalid input/ops, double map or lower
 * failure: GF2_EMAP; absent reservation or registry/ch mismatch: GF3_ESTATE.
 * On any failure view and output remain unchanged. */
int gf_vram_view_map(struct gf_vram_view *view, struct gf2_chan *ch,
                     u32 off, u32 len, volatile u8 **out);
/* Uses saved ops/owner/map_base/map_bytes. Success zeros all view fields.
 * NULL/unmapped/lower failure: GF2_EMAP, retaining all ownership for retry.
 * Keep reservation and borrowed adapter alive until successful unmap. */
int gf_vram_view_unmap(struct gf_vram_view *view);

struct gf_vram_reservation {
    const void *owner;
    u32 off, bytes, reservation_id, generation;
};
/* Owner supplies zero-initialized static/resident nodes; registered nodes
 * and their non-NULL owner identity addresses stay alive and immovable.
 * Based on nouveau_bo.c:594-599,618-629 pin/unpin ownership, replacing
 * BO/TTM allocation with explicit OS32 ranges and borrowed linked nodes. */
struct gf_vram_reservation_node {
    struct gf_vram_reservation value;
    struct gf_vram_reservation_node *next;
};
struct gf_vram_registry {
    struct gf2_chan *ch;
    struct gf_vram_reservation_node *head;
    u32 next_id;
};
/* V0 owns one static registry, initially zero, for one production channel
 * (also one ch per host process). First init: ch=argument, head=NULL, next_id=1.
 * Same ch: unchanged success; different ch: GF3_ESTATE; NULL: GF3_EINVAL.
 * C1 initializes before first reserve/publication. Never reset on recover. */
int gf_vram_registry_init(struct gf2_chan *ch);
/* Registers an explicit nonempty range below push B and terminal instmem;
 * no placement search/heap/fixed entry limit. Reject overlaps/live nodes.
 * IDs start at 1, never reused; generation increments per node registration.
 * After U32_MAX ID or generation exhaustion, return GF2_ENOSPC, no wrapping.
 * Persistent windows and T0 test reservations are page aligned. */
int gf_vram_reserve(struct gf2_chan *ch, const void *owner,
                    u32 off, u32 bytes, struct gf_vram_reservation_node *node);
/* All five key fields must match a live entry. Caller first ends GPU/display
 * use and unmaps all windows. Clears next/owner/off/bytes, retains ID and
 * generation history. Failed stop/unmap retains reservations. F5 keeps the
 * same nodes/identities alive across same-layout save/restore. */
int gf_vram_release(struct gf2_chan *ch, const struct gf_vram_reservation *key);
/* ID/generation are mandatory; owner=NULL permits any owner for trusted
 * internal callers. Nonempty off/bytes may name a contained subrange.
 * Success copies the value, never lends a node. */
int gf_vram_match(struct gf2_chan *ch, const void *owner,
                  u32 reservation_id, u32 generation, u32 off, u32 bytes,
                  struct gf_vram_reservation *out);
/* Start cursor=0; returns 1 and copies the first ID greater than cursor,
 * advances cursor to that ID; returns 0 at end with both outputs unchanged.
 * No reserve/release during enumeration; errors are negative. */
int gf_vram_next(struct gf2_chan *ch, u32 *cursor,
                 struct gf_vram_reservation *out);
/* Registry success is 0 except next's item result 1. Failure leaves registry,
 * nodes and outputs unchanged: required NULL/bad size GF3_EINVAL; placement
 * or match containment GF3_ERANGE; overlap/live node GF2_EEXIST; identity
 * exhaustion GF2_ENOSPC; uninitialized/wrong ch/missing/stale/wrong owner
 * GF3_ESTATE. No locks, refcounts or automatic eviction. */
#endif
