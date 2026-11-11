#!/usr/bin/env python3
CHECK = dict(name='console-dual-host', tier=1, needs=[], budget=8,
    covers=['kernel/console/console_dual.c', 'kernel/console/console_dual.h',
            'platform/pc98/display_dual.c', 'kernel/console/console.c',
            'drivers/video/pc98/pc98_route.c', 'init/kernel.c',
            'tools/tests/console_dual_host.c', 'tools/tests/test_console_dual.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'tools/tests/legacy_guest_fake.h', 'kernel/console/console_backend.h',
            'include/console.h', 'include/con_sink.h', 'include/legacy_guest.h',
            'gfx/v2/gfx2_int.h', 'include/gfx_hal.h', 'include/gfx_hal2.h',
            'sdk/include/os32/os32_gfx2.h'])


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
