#!/usr/bin/env python3
CHECK = dict(name='trident-sync-host', tier=1, needs=[], budget=3.8,
    covers=['tools/tests/test_trident_sync.py', 'sys/include/copy_units.h', 'arch/*/arch_copy.h', 'sys/hal/gfx/sync/sync_int.h', 'sys/hal/gfx/v2/gfx2_int.h', 'sys/include/gfx_sync.h', 'sys/hal/gfx/gfx_hal2.h', 'sdk/include/os32/os32_gfx2.h', 'sys/dev/video/trident/present.c', 'sys/dev/video/trident/trident.h',
            'tools/tests/trident_sync_host.c', 'tools/tests/trident_sync_fake.c', 'sys/include/gfx_sync.h', 'sys/hal/gfx/sync/sync_int.h', 'sys/hal/gfx/v2/gfx2_int.h', 'tools/tests/trident_device_fake.c',
            'tools/tests/trident_device_fake.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'sys/dev/video/trident/regs.c',
            'sys/hal/gfx/gfx_internal.h', 'sys/hal/gfx/gfx.h', 'sys/include/gfx_hal.h', 'sys/include/gfx_format.h',
            'sys/include/gfx_mode.h', 'sys/kern/mm/kmalloc.h', 'sdk/include/os32/os32_kapi_shared.h',
            'sdk/include/os32/os32_generations.h', 'sys/include/types.h', 'sys/include/memmap.h',
            'sys/platform/pc98/pegc.h', 'sys/platform/pc98/pc98.h', 'sys/hal/gfx/palette.h', 'build/config.mk'],
    basis=['HAL_V2_DESIGN §10', 'HAL_V2_DESIGN §13-3', 'TRIDENT_FULL_DESIGN'],
)

def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_sync.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
