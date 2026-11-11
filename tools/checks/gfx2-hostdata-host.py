#!/usr/bin/env python3
CHECK = dict(name='gfx2-hostdata-host', tier=1,
    covers=['gfx/v2/sw_hostdata.c', 'gfx/v2/gfx2_sw.h', 'gfx/v2/gfx2_int.h',
            'include/**', 'mm/pgalloc.h', 'arch/*/arch_copy.h',
            'tools/tests/gfx2_hostdata_host.c', 'tools/tests/test_gfx2_hostdata.py',
            'tools/tests/host32.py'], needs=[], budget=5.0)


def run(ctx):
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_hostdata.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)
