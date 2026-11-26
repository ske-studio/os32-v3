#ifndef OS32_PC98_PLATFORM_KERNEL_ENTRY_H
#define OS32_PC98_PLATFORM_KERNEL_ENTRY_H
#ifdef OS32_NASM
#define BOOT_FUNCTION(result, name, args, attrs) extern name
#else
#include "types.h"
#define BOOT_FUNCTION(result, name, args, attrs) result name args attrs;
#endif

BOOT_FUNCTION(void __cdecl, kernel_main, (u32 mem_kb, u32 boot_drive), )

#undef BOOT_FUNCTION
#endif
