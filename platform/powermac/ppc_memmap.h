#ifndef OS32_PPC_MEMMAP_H
#define OS32_PPC_MEMMAP_H
#include "ppc_oea.h"
#include "ppc_layout.h"
/* Only reserved RAM below PM_BAT_RAM_END is identity mapped. */
static inline void *pm_p2v_ram(u32 pa) { return (void *)pa; }
static inline u32 pm_v2p_ram(const void *p) { return (u32)p; }
static inline u32 pm_p2v_io(u32 pa) { return PM_IO_ALIAS+(pa & ~PPC_PAGE_MASK); }
#endif
