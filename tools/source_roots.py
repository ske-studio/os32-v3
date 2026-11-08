"""Source scan roots. Preserve each check's scope when directories move."""

SCAN_ROOTS = {
    'p2v': ('platform/pc98/legacy', 'platform/pc98/bootinfo_capture.c', 'platform/pc98/bootinfo_check.c', 'platform/pc98/bootinfo_geom.c', 'platform/pc98/bootinfo_report.c', 'platform/pc98/bios_snapshot.c', 'platform/pc98/boot_capture.c', 'init', 'mm', 'arch/x86/kernel', 'kernel', 'kdebug', 'drivers', 'gfx', 'fs', 'exec', 'kapi', 'lib'),
    'arm_compile': ('init', 'mm', 'arch/x86/kernel', 'kernel', 'drivers', 'fs', 'exec', 'kapi', 'lib', 'net', 'gfx'),
    'arch_asm': ('init', 'mm', 'kernel', 'kdebug', 'drivers', 'exec', 'fs', 'kapi', 'lib',
                 'net', 'gfx', 'include', 'arch', 'platform'),
    'selected_compile': ('init', 'mm', 'arch', 'platform', 'kernel', 'drivers', 'fs', 'exec', 'kapi', 'lib', 'net', 'gfx'),
    'kprintf_c': ('userland', 'sdk'),
    'kprintf_rust': ('sdk/rust', 'userland'),
}
