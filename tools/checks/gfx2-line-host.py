#!/usr/bin/env python3
CHECK = dict(name='gfx2-line-host', tier=1,
    covers=['sys/hal/gfx/v2/sw_line.c', 'sys/hal/gfx/v2/gfx2_int.h', 'sys/hal/gfx/v2/gfx2_sw.h', 'include/**', 'sys/kern/mm/pgalloc.h',
            'sys/kern/mm/paging.h', 'sys/kern/kstring.h', 'arch/*/arch_copy.h',
            'tools/tests/gfx2_line_host.c', 'tools/tests/test_gfx2_line.py',
            'tools/tests/host32.py', 'tools/tests/mutpar.py'],
    needs=[], budget=3.0,
    basis=['HAL_V2_DESIGN §13-1'],
)


def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_gfx2_line.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
