#!/usr/bin/env python3
CHECK = dict(name='trident-sync-host', tier=1, needs=[], budget=3.8,
    covers=['tools/tests/test_trident_sync.py', 'include/copy_units.h', 'arch/*/arch_copy.h', 'gfx/sync/sync_int.h', 'gfx/v2/gfx2_int.h', 'include/gfx_sync.h', 'include/gfx_hal2.h', 'sdk/include/os32/os32_gfx2.h', 'drivers/video/trident/present.c', 'drivers/video/trident/trident.h',
            'tools/tests/trident_sync_host.c', 'tools/tests/trident_sync_fake.c', 'include/gfx_sync.h', 'gfx/sync/sync_int.h', 'gfx/v2/gfx2_int.h', 'tools/tests/trident_device_fake.c',
            'tools/tests/trident_device_fake.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'drivers/video/trident/regs.c',
            'gfx/gfx_internal.h', 'gfx/gfx.h', 'include/gfx_hal.h', 'include/gfx_format.h',
            'include/gfx_mode.h', 'mm/kmalloc.h', 'sdk/include/os32/os32_kapi_shared.h',
            'sdk/include/os32/os32_generations.h', 'include/types.h', 'include/memmap.h',
            'include/pegc.h', 'include/pc98.h', 'gfx/palette.h', 'build/config.mk'])

def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_sync.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
