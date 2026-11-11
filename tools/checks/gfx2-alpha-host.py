#!/usr/bin/env python3
CHECK = dict(name='gfx2-alpha-host', tier=1, needs=[], budget=5,
    covers=['gfx/v2/sw_alpha.c', 'gfx/v2/gfx2_sw.h', 'gfx/v2/gfx2_int.h',
            'include/**', 'mm/pgalloc.h', 'tools/tests/gfx2_alpha_host.c',
            'tools/tests/test_gfx2_alpha.py', 'tools/tests/host32.py'])

def run(ctx):
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_alpha.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)
