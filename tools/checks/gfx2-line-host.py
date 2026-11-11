#!/usr/bin/env python3
CHECK = dict(name='gfx2-line-host', tier=1,
    covers=['gfx/v2/sw_line.c', 'gfx/v2/gfx2_int.h', 'gfx/v2/gfx2_sw.h', 'include/**', 'mm/pgalloc.h',
            'mm/paging.h', 'lib/kstring.h', 'arch/*/arch_copy.h',
            'tools/tests/gfx2_line_host.c', 'tools/tests/test_gfx2_line.py',
            'tools/tests/host32.py', 'tools/tests/mutpar.py'],
    needs=[], budget=3.0)


def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_gfx2_line.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
