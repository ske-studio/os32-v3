#!/usr/bin/env python3
CHECK = dict(name='gf-v2-gart-host', tier=1, needs=[], budget=5,
    covers=['drivers/video/geforce/gf_v2_gart.c', 'drivers/video/geforce/gf_v2.h',
            'hwdev/gf/accel.h', 'hwdev/gf/command.h', 'hwdev/gf/foundation.h',
            'hwdev/gf/vram_view.h', 'hwdev/gf/vram_layout.h',
            'gfx/v2/gfx2_fmt.h', 'gfx/v2/gfx2_sw.h', 'gfx/v2/gfx2_int.h',
            'include/gfx_hal.h', 'include/gfx_hal2.h', 'include/memmap.h',
            'include/types.h', 'include/type_contract.h',
            'include/copy_units.h', 'arch/*/arch_copy.h',
            'sdk/include/os32/os32_gfx2.h', 'sdk/include/os32/os32_kapi_shared.h',
            'tools/tests/test_gf_v2_gart.py', 'tools/tests/gf_v2_gart_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'])


def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_v2_gart.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
