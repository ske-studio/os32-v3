#ifndef OS32_PPC_KERNEL_ENTRY_H
#define OS32_PPC_KERNEL_ENTRY_H
/* PPC32 caller linkage and reserved space, rounded to 16 bytes. */
#define PPC_KERNEL_ENTRY_FRAME 32
#ifndef __ASSEMBLER__
#include "ppc_boot.h"
/* H2 supplies the body; returning parks at ppc_kernel_entry_stop. */
void ppc_kernel_start(ppc_ci ci);
#endif
#endif
