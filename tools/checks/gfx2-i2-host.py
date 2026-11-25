#!/usr/bin/env python3
CHECK = dict(name='gfx2-i2-host', tier=2, needs=['make:all'], budget=60,
    covers=['tools/tests/test_gfx2_i2.py', 'tools/tests/gfx2_i2_host.c', 'tools/tests/gfx2_i2_core_host.h',
            'tools/tests/test_gfx2_i1.py', 'tools/tests/gfx2_i1_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'tools/tests/kernel_layout.py', 'tools/tests/paging_source.py',
            'tools/tests/pgalloc_host_fixture.h', 'tools/tests/host_arch/**',
            'sys/hal/gfx/gfx_core.c', 'gfx/gfx_core_*.inc', 'sys/hal/gfx/gfx_internal.h',
            'sys/platform/pc98/video/pc98_route.h', 'sys/platform/pc98/video/splash_raster.h',
            'sys/kern/exec/appslot.c', 'sys/kern/exec/appslot_selftest.inc', 'sys/kern/exec/surface_query.h', 'sys/kern/exec/exec.h',
            'gfx/v2/**', 'gfx/sync/**', 'gfx/palette.*', 'sys/hal/gfx/gfx.h', 'sys/hal/gfx/gfx_policy.h',
            'mm/**', 'include/**', 'sdk/include/**', 'lib/kstring*',
            'lib/dma_contract*', 'lib/divdi3.c', 'kernel/module/**', 'sys/kern/console/display_restore.h',
            'exec/lease.*', 'sys/kern/exec/appslot.h', 'build/os32.ld',
            'userland/lib/gfx/**',
            'build/app.conf', 'build/programs.mk', 'userland/diag/gfx2_i1.c',
            'userland/diag/gfx2_i2.c', 'userland/deploy.yaml'],
    basis=['HAL_V2_DESIGN §13', 'HAL_V2_DESIGN §5-3'],
    env_limit='fake GPUと固定時計でhw/sw混在submitを確認するが、実GPUのDMA完了遅延と競合は再現しない。',
)

def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_i2.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
