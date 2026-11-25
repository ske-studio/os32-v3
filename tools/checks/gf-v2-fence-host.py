#!/usr/bin/env python3
CHECK = dict(name='gf-v2-fence-host', tier=1, needs=[], budget=5,
    covers=['sys/dev/video/geforce/gf_v2_fence.c', 'sys/dev/video/geforce/gf_v2.h',
            'hwdev/gf/*.h', 'gfx/v2/*.h', 'include/*.h', 'arch/*/arch_copy.h',
            'sdk/include/os32/*.h', 'tools/tests/gf_v2_fence_host.c',
            'tools/tests/test_gf_v2_fence.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py'],
    basis=['HAL_V2_DESIGN §6', 'HAL_V2_DESIGN §11-6', 'HAL_V2_DESIGN §13-1'],
)

def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_v2_fence.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
