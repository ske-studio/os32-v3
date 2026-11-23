"""PowerMac HW_OP §5 stages 4/5a/5b."""
CHECK = dict(name='ppc-hwb-commands', tier=2, needs=['make:ppc-ci-image'], budget=1200,
    env=['ppc-cross', 'qemu-ppc'], full_only=True,
    covers=['tools/checks/ppc-hwb-commands.py',
            'tools/ppc/**', 'tools/*.py', 'userland/ppc/**', 'userland/shell/**',
            'Makefile', 'build/**', 'sdk/**', 'mm/**', 'exec/**', 'arch/ppc/**',
            'platform/powermac/**', 'include/**', 'init/**', 'kernel/**',
            'kapi/**', 'lib/**', 'fs/**', 'drivers/**', 'input/**', 'net/**',
            'userland/tests/hw/**', 'gfx/**', 'kdebug/**', 'assets/fonts/**'],
    basis=['docs/decisions/realhw/HWOP_COMMON_DESIGN.md §2.4, §3'],
    env_limit='Requires OpenBIOS, genisoimage and mac99 QEMU.')


def run(ctx):
    import os
    import shutil
    import subprocess
    from pathlib import Path

    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if (not (cross / 'bin/powerpc-elf-gcc').is_file() or
            not Path('/usr/share/qemu/openbios-ppc').is_file() or
            not all(shutil.which(tool) for tool in ('qemu-system-ppc', 'genisoimage'))):
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP') + ' ppc-hwb-commands: mac99 tools unavailable')
        return int(required)

    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    return subprocess.call(['python3', '-B', 'tools/ppc/hwb_commands_accept.py'] +
                         (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
