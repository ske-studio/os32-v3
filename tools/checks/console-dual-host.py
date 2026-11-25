#!/usr/bin/env python3
CHECK = dict(name='console-dual-host', tier=1, needs=[], budget=8,
    covers=['sys/kern/console/console_dual.c', 'sys/kern/console/console_dual.h',
            'sys/platform/pc98/display_dual.c', 'sys/kern/console/console.c',
            'sys/platform/pc98/video/pc98_route.c', 'sys/platform/pc98/kernel.c', 'sys/hal/gfx/gfx_core.c',
            'tools/tests/console_dual_host.c', 'tools/tests/test_console_dual.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'tools/tests/legacy_guest_fake.h', 'sys/kern/console/console_backend.h',
            'sys/include/console.h', 'sys/include/con_sink.h', 'sys/include/legacy_guest.h',
            'sys/hal/gfx/v2/gfx2_int.h', 'sys/include/gfx_hal.h', 'sys/hal/gfx/gfx_hal2.h',
            'sdk/include/os32/os32_gfx2.h'],
    basis=['CONSOLE_DUAL_DESIGN §2', 'CONSOLE_DUAL_DESIGN §4', 'CONSOLE_DUAL_DESIGN §8', 'GF_STOP_REF_DESIGN §4'],
)


def run(ctx):
    import os
    import subprocess
    if not ctx.runners:
        return 1
    for i, runner in enumerate(ctx.runners):
        argv = ['python3', '-B', 'tools/tests/test_console_dual.py']
        if ctx.mutate and i == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root,
                             env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    import pathlib
    import subprocess
    import sys
    root = pathlib.Path(__file__).resolve().parents[2]
    raise SystemExit(subprocess.call(
        [sys.executable, '-B', str(root / 'tools/tests/test_console_dual.py'), *sys.argv[1:]],
        cwd=root))
