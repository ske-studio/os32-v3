#!/usr/bin/env python3
CHECK = dict(name='gf-v2-gart-host', tier=1, needs=[], budget=5,
    covers=['sys/dev/video/geforce/gf_v2_gart.c', 'sys/dev/video/geforce/gf_v2.h',
            'sys/dev/video/geforce/accel.h', 'sys/dev/video/geforce/command.h', 'sys/dev/video/geforce/foundation.h',
            'sys/dev/video/geforce/vram_view.h', 'sys/dev/video/geforce/vram_layout.h',
            'sys/hal/gfx/v2/gfx2_fmt.h', 'sys/hal/gfx/v2/gfx2_sw.h', 'sys/hal/gfx/v2/gfx2_int.h',
            'sys/include/gfx_hal.h', 'sys/hal/gfx/gfx_hal2.h', 'sys/include/memmap.h',
            'sys/include/types.h', 'sys/include/type_contract.h',
            'sys/include/copy_units.h', 'arch/*/arch_copy.h',
            'sdk/include/os32/os32_gfx2.h', 'sdk/include/os32/os32_kapi_shared.h',
            'tools/tests/test_gf_v2_gart.py', 'tools/tests/gf_v2_gart_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    basis=['HAL_V2_DESIGN §8', 'HAL_V2_DESIGN §11-6', 'HAL_V2_DESIGN §13-1'],
)


def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_v2_gart.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
