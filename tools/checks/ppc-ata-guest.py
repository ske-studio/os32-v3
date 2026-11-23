CHECK = dict(name='ppc-ata-guest', tier=2, needs=['make:ppc-ci-image'], budget=1200,
    env=['ppc-cross', 'qemu-ppc'], full_only=True,
    covers=['tools/checks/ppc-ata-guest.py',
            'tools/ppc/**', 'tools/*.py', 'userland/ppc/**', 'userland/shell/**',
            'Makefile', 'build/**', 'sdk/**', 'mm/**', 'exec/**', 'arch/ppc/**',
            'platform/powermac/**', 'include/**', 'init/**', 'kernel/**',
            'kapi/**', 'lib/**', 'fs/**', 'drivers/**', 'input/**', 'net/**',
            'gfx/**', 'assets/fonts/**'],
    basis=['docs/ppc/ata.md §3; ATA IDENTIFY words 27–46 and 60–61; B05 の段 3 の安全の決まり (11-23 PM)'],
    env_limit='Requires OpenBIOS, genisoimage and mac99 QEMU.')


def run(ctx):
    import os
    import subprocess

    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    rc = subprocess.call(['python3', '-B', 'tools/ppc/ata_accept.py'] +
                         (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
    # rc 3 = toolchain or emulator absent; only a failure when the PPC environment is required.
    return 0 if rc == 3 and os.environ.get('OS32_REQUIRE_PPC') != '1' else rc
