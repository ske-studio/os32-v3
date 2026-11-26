#ifndef OS32_POWERMAC_PLATFORM_KERNEL_TAKEOVER_H
#define OS32_POWERMAC_PLATFORM_KERNEL_TAKEOVER_H
#ifdef __ASSEMBLER__
#define BOOT_FUNCTION(result, name, args, attrs) .extern name
#else
struct pm_kboot_info;
#define BOOT_FUNCTION(result, name, args, attrs) result name args attrs;
#endif

BOOT_FUNCTION(void, ppc_kernel_real_start, (const struct pm_kboot_info *info), __attribute__((noreturn)))

#undef BOOT_FUNCTION
#endif
