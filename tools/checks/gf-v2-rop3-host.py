#!/usr/bin/env python3
CHECK = dict(name='gf-v2-rop3-host', tier=1, needs=[], budget=5,
    covers=['hwdev/gf/accel_rop3.c', 'hwdev/gf/*.h', 'include/*.h',
            'arch/*/arch_copy.h', 'sdk/include/os32/*.h',
            'tools/tests/gf_v2_rop3_host.c', 'tools/tests/test_gf_v2_rop3.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'])

def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_v2_rop3.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
