#ifndef OS32_PC98_PLATFORM_V86_ISR_H
#define OS32_PC98_PLATFORM_V86_ISR_H
#ifdef OS32_NASM
#define BOOT_FUNCTION(result, name, args, attrs) extern name
#else
#include "types.h"
#define BOOT_FUNCTION(result, name, args, attrs) result name args attrs;
#endif

BOOT_FUNCTION(int, v86_gp_handler, (u32 *frame), )
BOOT_FUNCTION(void, v86_fault_handler, (u32 *frame, u32 vector), )
BOOT_FUNCTION(void, v86_reflect_irq, (u32 *frame, u32 irq), )
BOOT_FUNCTION(int, v86_tick_and_check_timeout, (void), )
BOOT_FUNCTION(int, v86_check_exit_request, (void), )
BOOT_FUNCTION(void, v86_exit_to_kernel, (void), )

#undef BOOT_FUNCTION
#endif
