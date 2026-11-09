/* Kernel-only trap boundary. Call registration with EE=0 on the boot CPU. */
#ifndef OS32_PPC_TRAP_H
#define OS32_PPC_TRAP_H
#include "ppc_frame.h"
#include "ppc_pd.h"
enum ppc_trap_reason {
    PPC_TRAP_UNKNOWN = 1, PPC_TRAP_DSI_MISS, PPC_TRAP_DSI_PROTECT,
    PPC_TRAP_DSI_IO, PPC_TRAP_ISI, PPC_TRAP_PROGRAM,
    PPC_TRAP_ALIGNMENT, PPC_TRAP_SC, PPC_TRAP_UNREGISTERED,
    PPC_TRAP_USER,
    PPC_TRAP_ISI_PROTECT, PPC_TRAP_ISI_NOEXEC,
    PPC_TRAP_PROGRAM_FP, PPC_TRAP_PROGRAM_ILLEGAL,
    PPC_TRAP_PROGRAM_PRIVILEGED, PPC_TRAP_PROGRAM_TRAP,
    PPC_TRAP_UNSUPPORTED = 0x100
};
void ppc_trap_set_handler(u32 vector, void (*handler)(struct ppc_frame *));
/* Reader must access software PD/PT physical words via mapped RAM, DR=1.
 * NULL disables walking. Context and table mappings must outlive dispatch. */
void ppc_trap_set_reader(ppc_pd_read_fn reader, void *context);
/* Enable only after root and the sink's I/G MMIO alias are ready. */
void ppc_trap_set_fatal_ready(int ready);
/* Explicit H2/platform provider; never called in real mode or before ready.
 * Dump the complete frame and reason, using only the prepared I/G alias. */
void ppc_trap_fatal_sink(const struct ppc_frame *frame, u32 reason);
/* Returns 0 if occupied, unaligned, or not a supported negative probe kind. */
int ppc_trap_onfault(u32 pc, u32 va, u32 reason, u32 resume);
void ppc_trap_onfault_clear(void);
#endif
