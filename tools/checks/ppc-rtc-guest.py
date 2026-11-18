CHECK = dict(name='ppc-rtc-guest', tier=2, needs=[], budget=1200,
    env=['ppc-cross', 'qemu-ppc', 'socket'], full_only=True,
    covers=['tools/checks/ppc-rtc-guest.py', 'tools/accept/ppc_rtc.py', 'tools/accept/ppc_shm.py',
            'tools/ppc/**', 'tools/*.py', 'userland/ppc/**', 'userland/shell/**',
            'Makefile', 'build/**', 'sdk/**', 'mm/**', 'exec/**', 'arch/ppc/**',
            'platform/powermac/**', 'include/**', 'init/**', 'kernel/**',
            'kapi/**', 'lib/**', 'fs/**', 'drivers/**', 'input/**', 'net/**',
            'gfx/**', 'assets/fonts/**'],
    basis=['docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md §5 clock'],
    env_limit='Requires mac99 QEMU and loopback UDP/TCP for link-sh RSH.')


def run(ctx):
    import os
    import subprocess

    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    rc = subprocess.call(['python3', '-B', 'tools/accept/ppc_rtc.py'] +
                         (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
    # rc 3 = toolchain/QEMU/socket absent; only a failure when the PPC environment is required.
    return 0 if rc == 3 and os.environ.get('OS32_REQUIRE_PPC') != '1' else rc
