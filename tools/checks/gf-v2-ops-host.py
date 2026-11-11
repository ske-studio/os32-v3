#!/usr/bin/env python3
CHECK = dict(name='gf-v2-ops-host', tier=1, needs=[], budget=15,
    covers=['drivers/video/geforce/gf_v2*.c', 'drivers/video/geforce/gf_v2.h',
        'hwdev/gf/accel_rop*.c', 'hwdev/gf/*.h', 'gfx/v2/*.h', 'include/*.h',
        'arch/*/arch_copy.h', 'sdk/include/os32/*.h', 'tools/tests/gf_v2_ops_host.c',
        'tools/tests/test_gf_v2_ops.py', 'tools/tests/host32.py',
        'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py'])
def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_v2_ops.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
