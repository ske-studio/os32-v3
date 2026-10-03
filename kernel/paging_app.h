/* T2f f3/f4 private transactions; host-only until f5. */
#ifndef PAGING_APP_H
#define PAGING_APP_H
#include "paging.h"

/* Stack-local, never embedded in AS/AppSlot. PFNs of staged data live in NP PTEs. */
struct paging_app_stage {
    u32 pending[MEM_APP_BAND_MAX_PDES];
    struct addrspace *as;
    u32 base, end;
};
STATIC_ASSERT(sizeof(((struct paging_app_stage *)0)->pending) == 256, app_pending_size);
extern u32 paging_app_bad_free_count;
extern u32 paging_app_pt_nospc_count, paging_app_data_nospc_count;
int paging_app_context(const struct addrspace *as);
/* Caller serializes context/prepare/stage/commit or abort without callbacks,
 * reentry or AS switches. stage requires IF=1, depth=0 and an empty range.
 * Pending PT PTEs become PRESENT only after all allocations succeed,
 * before IRQ save; they remain unreachable until commit publishes the PDE.
 * Failed stage retains no allocations. Only successful stage may be aborted
 * explicitly or committed. */
int paging_app_stage(struct paging_app_stage *tx, struct addrspace *as,
                     u32 base, u32 end);
void paging_app_abort(struct paging_app_stage *tx);
/* Caller holds saved IRQ across commit -> extent publish -> active CR3 reload.
 * commit does no allocation/free; the caller restores IF after publication. */
void paging_app_commit(struct paging_app_stage *tx);
STATIC_ASSERT(MEM_APP_BAND_MAX_PDES == 2 * sizeof(u32) * 8, app_unmap_mask_capacity);
/* Empty PT membership only; data PFNs remain in the NP PTEs. */
struct paging_app_unmap {
    struct addrspace *as;
    u32 base, end, empty_lo, empty_hi;
};
extern u32 paging_app_unmap_reject_count;
int paging_app_unmap_prepare(struct paging_app_unmap *tx, struct addrspace *as,
                             u32 base, u32 end);
/* NP/PDE withdrawal and one active reload in a short saved-IRQ interval.
 * Data returns/zeroing and empty PT returns require IF=1 afterwards. */
void paging_app_unmap_withdraw(struct paging_app_unmap *tx);
int paging_app_unmap_free(struct paging_app_unmap *tx);
#endif
