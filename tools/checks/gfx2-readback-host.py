#!/usr/bin/env python3
CHECK = dict(name='gfx2-readback-host', tier=1,
    covers=['gfx/v2/sw_readback.c', 'gfx/v2/gfx2_sw.h', 'gfx/v2/gfx2_int.h',
            'include/gfx_sync.h', 'sdk/include/os32/os32_gfx2.h',
            'tools/tests/gfx2_readback_host.c', 'tools/tests/test_gfx2_readback.py',
            'tools/tests/host32.py', 'tools/tests/mutpar.py'],
    needs=[], budget=3.0)


def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_gfx2_readback.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
