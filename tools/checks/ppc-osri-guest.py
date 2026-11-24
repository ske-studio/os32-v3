"""KAPI_SPEC §4-16 and DESIGN.final §6 specify guest return values and scanout."""
CHECK = dict(name='ppc-osri-guest', tier=2, needs=[], budget=360,
    env=['ppc-cross', 'qemu-ppc', 'socket'], full_only=True,
    covers=['tools/checks/ppc-osri-guest.py', 'tools/accept/ppc_display.py', 'tools/accept/ppc_shm.py',
            'tools/ppc/**', 'tools/*.py', 'userland/ppc/**', 'userland/shell/**',
            'Makefile', 'build/**', 'sdk/**', 'mm/**', 'exec/**', 'arch/ppc/**',
            'platform/powermac/**', 'include/**', 'init/**', 'kernel/**',
            'kapi/**', 'lib/**', 'fs/**', 'drivers/**', 'input/**', 'net/**',
            'gfx/**', 'assets/fonts/**'],
    basis=['docs/KAPI_SPEC.md §4-16 gfx_osri_show'],
    env_limit='Requires mac99 QEMU and loopback UDP/TCP for link-sh RSH.')


def run(ctx):
    import os
    import subprocess

    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    rc = subprocess.call(['python3', '-B', 'tools/accept/ppc_display.py', '--osri'] +
                         (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
    # rc 3 = toolchain/QEMU/socket absent; only a failure when the PPC environment is required.
    return 0 if rc == 3 and os.environ.get('OS32_REQUIRE_PPC') != '1' else rc
