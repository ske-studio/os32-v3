#!/usr/bin/env python3
CHECK = dict(name='gfx2-i2-host', tier=2, needs=['make:all'], budget=60,
    covers=['tools/tests/test_gfx2_i2.py', 'tools/tests/gfx2_i2_host.c', 'tools/tests/gfx2_i2_core_host.h',
            'tools/tests/test_gfx2_i1.py', 'tools/tests/gfx2_i1_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'tools/tests/kernel_layout.py', 'tools/tests/paging_source.py',
            'tools/tests/pgalloc_host_fixture.h', 'tools/tests/host_arch/**',
            'gfx/gfx_core.c', 'gfx/gfx_core_*.inc', 'gfx/gfx_internal.h',
            'drivers/video/pc98/pc98_route.h', 'drivers/video/pc98/splash_raster.h',
            'exec/appslot.c', 'exec/appslot_selftest.inc', 'exec/surface_query.h', 'exec/exec.h',
            'gfx/v2/**', 'gfx/sync/**', 'gfx/palette.*', 'gfx/gfx.h', 'gfx/gfx_policy.h',
            'mm/**', 'include/**', 'sdk/include/**', 'lib/kstring*',
            'lib/dma_contract*', 'lib/divdi3.c', 'kernel/module/**', 'kernel/console/display_restore.h',
            'exec/lease.*', 'exec/appslot.h', 'build/os32.ld',
            'userland/lib/gfx/**',
            'build/app.conf', 'build/programs.mk', 'userland/tests/gfx2_i1.c',
            'userland/tests/gfx2_i2.c', 'userland/deploy.yaml'])

def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_i2.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
