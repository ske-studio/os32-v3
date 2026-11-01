/* Single-CPU MTRR WC lease; no PTE, PAT or device writes. */
#ifndef X86_MTRR_WC_H
#define X86_MTRR_WC_H

#include "types.h"

struct x86_wc_lease {
    u32 slot, owned, active;
    unsigned long long old_base, old_mask;
};

/* One caller, cooperative switching, drawing stopped throughout both calls.
 * Pass an inactive, initially zeroed lease. base/bytes describe one aligned,
 * power-of-two variable MTRR range above the fixed MTRR area (>= 1 MiB).
 * The caller validates BAR ownership, nonintersection, aliases, existing PTEs
 * (PCD=1/PWT=0/PAT=0), and BAR0's effective UC before calling.
 *
 * acquire: 1 = WC (owned=1: a previously V=0 slot; owned=0: BIOS WC),
 *          0 = unavailable, or failed readback with verified rollback (UC),
 *         -1 = rollback unverified; fatal, never continue as a UC fallback.
 * On -1 the active owned lease retains the saved pair for recovery.
 * A borrowed lease leaves every MSR unchanged, including on release.
 * PAT-capable CPUs must have PAT2=UC-; PAT is only read.
 */
int x86_wc_acquire(u32 base, u32 bytes, struct x86_wc_lease *lease);

/* release: 0 = original pair verified, borrowed WC drained, or inactive no-op;
 *         -1 = restore unverified (lease stays active; caller must stop).
 * CPU-side drain also runs for a borrowed lease after device identity failure;
 * no device access, allocation, logging or cooperative switch occurs here.
 * Unsupported CPUs never reach CR4/MSR/WBINVD operations.
 */
int x86_wc_release(struct x86_wc_lease *lease);

#endif
