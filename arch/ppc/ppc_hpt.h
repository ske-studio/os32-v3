/* Kernel plan-D HPT. All calls require EE=IR=DR=0 and physical code/stack. */
#ifndef OS32_PPC_HPT_H
#define OS32_PPC_HPT_H
#include "types.h"
struct ppc_frame;
/* Nonzero PA, exactly 128KiB, equally aligned, outside the allocator. Failure preserves state.
 * init/clear only zero HPT memory; C3 owns the complete TLB invalidation sequence. */
int ppc_hpt_init(u32 pa, u32 size);
void ppc_hpt_clear(void);
u32 ppc_hpt_sdr1(void);
/* C4 must load SR[segment] = PPC_SR_KP | ppc_hpt_vsid(segment), Ks=0. */
u32 ppc_hpt_vsid(u32 segment);
int ppc_hpt_spill(struct ppc_frame *f);
/* C3 provider: current software PD physical address. */
u32 ppc_root_current(void);
#endif
