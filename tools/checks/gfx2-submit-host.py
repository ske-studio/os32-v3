#!/usr/bin/env python3
CHECK = dict(name='gfx2-submit-host', tier=1, needs=[], budget=15,
    covers=['sys/hal/gfx/v2/submit.c', 'sys/hal/gfx/v2/check_op.c', 'sys/hal/gfx/v2/sw_table.c',
            'sys/hal/gfx/v2/gfx2_int.h', 'sys/hal/gfx/v2/gfx2_sw.h', 'include/**', 'sys/kern/kstring.h',
            'sys/kern/exec/exec.h', 'sys/kern/exec/surface_limits.h', 'sys/hal/gfx/gfx.h',
            'sdk/include/os32/**', 'sys/kern/mm/pgalloc.h', 'sys/kern/mm/paging.h', 'sys/kern/mm/physmem.h',
            'tools/tests/gfx2_submit_host.c', 'tools/tests/test_gfx2_submit.py',
            'tools/tests/host32.py'],
    basis=['HAL_V2_DESIGN §5-3', 'HAL_V2_DESIGN §12-1'],
)


def run(ctx):
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_submit.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)
