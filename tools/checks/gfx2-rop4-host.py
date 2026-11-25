#!/usr/bin/env python3
CHECK = dict(name='gfx2-rop4-host', tier=1, needs=[], budget=5,
    covers=['sys/hal/gfx/v2/sw_rop4.c', 'sys/hal/gfx/v2/gfx2_sw.h', 'sys/hal/gfx/v2/gfx2_int.h',
            'include/**', 'sdk/include/os32/os32_gfx2.h', 'sys/kern/mm/pgalloc.h',
            'tools/tests/gfx2_rop4_host.c', 'tools/tests/test_gfx2_rop4.py',
            'tools/tests/host32.py'],
    basis=['HAL_V2_DESIGN §13-1'],
)


def run(ctx):
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_rop4.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)
