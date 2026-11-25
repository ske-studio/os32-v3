"""Source scan roots. Preserve each check's scope when directories move."""

SCAN_ROOTS = {
    'p2v': ('sys/platform/pc98/gfx_scroll.c', 'sys/platform/pc98/gfx_init.c', 'sys/platform/pc98/gfx_selector.c', 'sys/platform/pc98/gfx_surfaces.c', 'platform/pc98/legacy', 'sys/platform/pc98/bootinfo_capture.c', 'sys/platform/pc98/bootinfo_check.c', 'sys/platform/pc98/bootinfo_geom.c', 'sys/platform/pc98/bootinfo_report.c', 'sys/platform/pc98/bios_snapshot.c', 'sys/platform/pc98/boot_capture.c', 'sys/platform/pc98/kernel.c', 'sys/platform/pc98/boot_platform.c', 'sys/platform/pc98/boot_display.c', 'sys/platform/pc98/boot_splash.c', 'sys/platform/pc98/boot_platform.h', 'sys/platform/pc98/boot_display.h', 'sys/platform/pc98/boot_splash.h', 'sys/platform/pc98/kernel_boot_cpu.inc', 'sys/platform/pc98/kernel_boot_memory.inc', 'sys/platform/pc98/kernel_gfx_config.inc', 'sys/platform/pc98/kernel_gui_shlib.inc', 'sys/platform/pc98/kernel_shell_fdd.inc', 'sys/platform/pc98/boot_serial.inc', 'sys/platform/powermac/ppc_kernel.c', 'init', 'mm', 'arch/x86/kernel', 'kernel', 'kdebug', 'drivers', 'gfx', 'fs', 'exec', 'kapi', 'lib', 'arch/x86/video', 'sys/platform/pc98/ime_render_tvram.c', 'sys/platform/pc98/ime_display.c', 'sys/platform/pc98/sysclk.c', 'sys/platform/pc98/sysclk.h', 'sys/dev/hwdev/hwdev_module.c', 'sys/dev/hwdev/hwdev_module.h'),
    'arm_compile': ('sys/platform/pc98/kernel.c', 'sys/platform/pc98/boot_platform.c', 'sys/platform/pc98/boot_display.c', 'sys/platform/pc98/boot_splash.c', 'sys/platform/powermac/ppc_kernel.c', 'init', 'mm', 'arch/x86/kernel', 'platform/pc98/legacy', 'kernel', 'drivers', 'fs', 'exec', 'kapi', 'lib', 'net', 'gfx', 'arch/x86/video'),
    'arch_asm': ('init', 'mm', 'kernel', 'kdebug', 'drivers', 'exec', 'fs', 'kapi', 'lib',
                 'net', 'gfx', 'include', 'arch', 'platform'),
    'selected_compile': ('init', 'mm', 'arch', 'platform', 'kernel', 'drivers', 'fs', 'exec', 'kapi', 'lib', 'net', 'gfx'),
    'kprintf_c': ('userland', 'sdk'),
    'kprintf_rust': ('sdk/rust', 'userland'),
}
