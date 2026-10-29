"""Source scan roots. Preserve each check's scope when directories move."""

SCAN_ROOTS = {
    'p2v': ('kernel', 'kdebug', 'drivers', 'gfx', 'fs', 'exec', 'kapi', 'lib'),
    'arm_compile': ('kernel', 'drivers', 'fs', 'exec', 'kapi', 'lib', 'net', 'gfx'),
    'arch_asm': ('kernel', 'kdebug', 'drivers', 'exec', 'fs', 'kapi', 'lib',
                 'net', 'gfx', 'include', 'arch', 'platform'),
    'kprintf_c': ('userland', 'sdk'),
    'kprintf_rust': ('sdk/rust', 'userland'),
}
