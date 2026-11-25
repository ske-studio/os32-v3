#!/usr/bin/env python3
import subprocess

CHECK = dict(name='gfx2-check-equiv-host', tier=2, needs=['make:all'], budget=20,
    covers=['tools/tests/of_fb_osri_fake.h', 'tools/tests/test_gfx2_check_equiv.py', 'tools/tests/gfx2_check_equiv_host.c',
            'tools/tests/test_gfx2_i1.py', 'tools/tests/gfx2_i1_host.c', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'tools/tests/kernel_layout.py', 'tools/tests/paging_source.py',
            'tools/tests/pgalloc_host_fixture.h', 'tools/tests/host_arch/**', 'gfx/v2/**',
            'sys/platform/pc98/video/pegc_v2.c', 'drivers/video/of_fb/**', 'gfx/sync/**', 'gfx/palette*', 'sys/hal/gfx/gfx.h', 'sys/hal/gfx/gfx_policy.h', 'mm/**', 'include/**',
            'sdk/include/**', 'lib/kstring*', 'lib/dma_contract.c', 'lib/divdi3.c', 'kernel/module/**', 'sys/hal/time/cpu_calibrate.h',
            'sys/hal/gfx/gfx_core.c', 'userland/diag/gfx2_i1.c', 'exec/**', 'build/os32.ld', 'sys/platform/pc98/kernel.c'],
    kind='characterization',
)

def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_check_equiv.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
