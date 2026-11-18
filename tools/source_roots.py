"""Source scan roots. Preserve each check's scope when directories move."""

SCAN_ROOTS = {
    'p2v': ('platform/pc98/gfx_scroll.c', 'platform/pc98/gfx_init.c', 'platform/pc98/gfx_selector.c', 'platform/pc98/gfx_surfaces.c', 'platform/pc98/legacy', 'platform/pc98/bootinfo_capture.c', 'platform/pc98/bootinfo_check.c', 'platform/pc98/bootinfo_geom.c', 'platform/pc98/bootinfo_report.c', 'platform/pc98/bios_snapshot.c', 'platform/pc98/boot_capture.c', 'platform/pc98/kernel.c', 'platform/pc98/boot_platform.c', 'platform/pc98/boot_display.c', 'platform/pc98/boot_splash.c', 'platform/pc98/boot_platform.h', 'platform/pc98/boot_display.h', 'platform/pc98/boot_splash.h', 'platform/pc98/kernel_boot_cpu.inc', 'platform/pc98/kernel_boot_memory.inc', 'platform/pc98/kernel_gfx_config.inc', 'platform/pc98/kernel_gui_shlib.inc', 'platform/pc98/kernel_shell_fdd.inc', 'platform/pc98/boot_serial.inc', 'platform/powermac/ppc_kernel.c', 'init', 'mm', 'arch/x86/kernel', 'kernel', 'kdebug', 'drivers', 'gfx', 'fs', 'exec', 'kapi', 'lib', 'arch/x86/video', 'platform/pc98/ime_render_tvram.c', 'platform/pc98/ime_display.c', 'platform/pc98/sysclk.c', 'platform/pc98/sysclk.h', 'hwdev/hwdev_module.c', 'hwdev/hwdev_module.h'),
    'arm_compile': ('platform/pc98/kernel.c', 'platform/pc98/boot_platform.c', 'platform/pc98/boot_display.c', 'platform/pc98/boot_splash.c', 'platform/powermac/ppc_kernel.c', 'init', 'mm', 'arch/x86/kernel', 'platform/pc98/legacy', 'kernel', 'drivers', 'fs', 'exec', 'kapi', 'lib', 'net', 'gfx', 'arch/x86/video'),
    'arch_asm': ('init', 'mm', 'kernel', 'kdebug', 'drivers', 'exec', 'fs', 'kapi', 'lib',
                 'net', 'gfx', 'include', 'arch', 'platform'),
    'selected_compile': ('init', 'mm', 'arch', 'platform', 'kernel', 'drivers', 'fs', 'exec', 'kapi', 'lib', 'net', 'gfx'),
    'kprintf_c': ('userland', 'sdk'),
    'kprintf_rust': ('sdk/rust', 'userland'),
}
