#!/usr/bin/env python3
CHECK = dict(name='ver-gfx-host', tier=1,
    covers=['include/gfx_hal.h', 'include/gfx_mode.h', 'include/gfx_format.h', 'gfx/gfx_core.c', 'tools/tests/test_ver_gfx.py', 'tools/tests/host32.py'],
    needs=[], budget=60.0)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_ver_gfx.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
