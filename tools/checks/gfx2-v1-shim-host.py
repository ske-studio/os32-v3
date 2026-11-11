#!/usr/bin/env python3
CHECK = dict(name='gfx2-v1-shim-host', tier=1, needs=[], budget=8,
    covers=['gfx/v2/v1_shim.c', 'gfx/v2/gfx2_int.h', 'include/gfx_hal.h',
            'include/gfx_hal2.h', 'include/gfx_sync.h', 'include/types.h',
            'include/type_contract.h', 'include/copy_units.h', 'arch/*/arch_copy.h',
            'include/memmap.h', 'mm/pgalloc.h', 'mm/paging.h',
            'sdk/include/os32/os32_gfx2.h', 'tools/tests/gfx2_v1_shim_host.c',
            'tools/tests/test_gfx2_v1_shim.py', 'tools/tests/host32.py'])


def run(ctx):
    import os
    import subprocess
    if not ctx.runners:
        return 1
    for i, runner in enumerate(ctx.runners):
        argv = ['python3', '-B', 'tools/tests/test_gfx2_v1_shim.py']
        if ctx.mutate and i == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root,
                             env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0
