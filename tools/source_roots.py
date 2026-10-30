"""Source scan roots. Preserve each check's scope when directories move."""

SCAN_ROOTS = {
    'p2v': ('init', 'mm', 'arch/x86/kernel', 'kernel', 'kdebug', 'drivers', 'gfx', 'fs', 'exec', 'kapi', 'lib'),
    'arm_compile': ('init', 'mm', 'arch/x86/kernel', 'kernel', 'drivers', 'fs', 'exec', 'kapi', 'lib', 'net', 'gfx'),
    'arch_asm': ('init', 'mm', 'kernel', 'kdebug', 'drivers', 'exec', 'fs', 'kapi', 'lib',
                 'net', 'gfx', 'include', 'arch', 'platform'),
    'kprintf_c': ('userland', 'sdk'),
    'kprintf_rust': ('sdk/rust', 'userland'),
}
