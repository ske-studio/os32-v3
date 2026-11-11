#!/usr/bin/env python3
CHECK = dict(name='gfx2-copy-host', tier=1, needs=[], budget=5,
    covers=['gfx/v2/sw_copy.c', 'gfx/v2/gfx2_sw.h', 'include/gfx_sync.h',
            'tools/tests/gfx2_copy_host.c', 'tools/tests/test_gfx2_copy.py',
            'tools/tests/host32.py'])


def run(ctx):
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_copy.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)
