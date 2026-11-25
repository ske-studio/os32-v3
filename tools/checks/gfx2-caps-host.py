#!/usr/bin/env python3
CHECK = dict(name='gfx2-caps-host', tier=1, needs=[], budget=8,
    covers=['sys/hal/gfx/v2/caps.c', 'sys/hal/gfx/v2/gfx2_int.h', 'sys/include/gfx_hal.h',
            'sys/kern/kstring.h', 'sys/hal/gfx/gfx_hal2.h', 'sys/include/gfx_sync.h', 'sys/include/types.h',
            'sys/include/type_contract.h', 'sys/include/copy_units.h', 'arch/*/arch_copy.h',
            'sys/include/memmap.h', 'sys/kern/mm/pgalloc.h', 'sys/kern/mm/paging.h',
            'sdk/include/os32/os32_gfx2.h', 'tools/tests/gfx2_caps_host.c',
            'tools/tests/test_gfx2_caps.py', 'tools/tests/host32.py'],
    basis=['HAL_V2_DESIGN §4-1', 'HAL_V2_DESIGN §12-1'],
)


def run(ctx):
    import os
    import subprocess
    if not ctx.runners:
        return 1
    for i, runner in enumerate(ctx.runners):
        argv = ['python3', '-B', 'tools/tests/test_gfx2_caps.py']
        if ctx.mutate and i == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root,
                             env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0
