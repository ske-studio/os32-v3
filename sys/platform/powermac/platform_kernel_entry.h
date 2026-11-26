#ifndef OS32_POWERMAC_PLATFORM_KERNEL_ENTRY_H
#define OS32_POWERMAC_PLATFORM_KERNEL_ENTRY_H
#ifdef __ASSEMBLER__
#define BOOT_FUNCTION(result, name, args, attrs) .extern name
#else
#include "ppc_boot.h"
#define BOOT_FUNCTION(result, name, args, attrs) result name args attrs;
#endif

BOOT_FUNCTION(void, ppc_kernel_start, (ppc_ci ci), )

#undef BOOT_FUNCTION
#endif
