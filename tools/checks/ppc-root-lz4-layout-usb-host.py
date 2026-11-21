"""OF boot and reservation expectations from PPC_BOOT_DESIGN and PPC_KBOOT_DESIGN."""
CHECK = dict(name='ppc-root-lz4-layout-usb-host', tier=2, budget=306,
    needs=['make:sdk/include/os32/os32_generations.h'],
    env=['ppc-cross', 'qemu-ppc'], full_only=True,
    covers=['tools/ppc/**', 'tools/checks/ppc-root-lz4-layout-usb-host.py', 'tools/accept/ppc_*.py',
            'tools/tests/ppc_sqlite_stack*', 'tools/*.py', 'tools/gen_unicode.c',
            'arch/x86/kernel/kentry.asm', 'boot/loader_fat_new.asm',
            'tools/tests/kernel_includes.py', 'tools/tests/owner_reclaim_host.c',
            'tools/tests/test_owner_reclaim.py',
            'Makefile', 'build/**', 'arch/ppc/**', 'platform/**', 'sdk/**', 'lib/**',
            'include/**', 'mm/**', 'kernel/**', 'fs/**', 'kapi/**', 'exec/**', 'init/**',
            'input/**', 'net/**', 'gfx/**', 'userland/**', 'drivers/**', 'assets/fonts/**'],
    basis=['docs/decisions/ppc/PPC_BOOT_DESIGN.md §§6–7',
           'docs/decisions/ppc/PPC_KBOOT_DESIGN.md §4'],
    env_limit='Linking the image inventory requires the PowerPC cross toolchain.')


def run(ctx):
    from ppc.root_lz4_group import run_group
    return run_group(ctx, 'usb')
