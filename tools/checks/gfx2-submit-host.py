#!/usr/bin/env python3
CHECK = dict(name='gfx2-submit-host', tier=1, needs=[], budget=15,
    covers=['gfx/v2/submit.c', 'gfx/v2/check_op.c', 'gfx/v2/sw_table.c',
            'gfx/v2/gfx2_int.h', 'gfx/v2/gfx2_sw.h', 'include/**', 'lib/kstring.h',
            'exec/exec.h', 'exec/surface_limits.h', 'gfx/gfx.h',
            'sdk/include/os32/**', 'mm/pgalloc.h', 'mm/paging.h', 'mm/physmem.h',
            'tools/tests/gfx2_submit_host.c', 'tools/tests/test_gfx2_submit.py',
            'tools/tests/host32.py'])


def run(ctx):
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_submit.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)
