#!/usr/bin/env python3
import subprocess

CHECK = dict(name='gfx2-check-equiv-host', tier=2, needs=['make:all'], budget=20,
    covers=['tools/tests/test_gfx2_check_equiv.py', 'tools/tests/gfx2_check_equiv_host.c',
            'tools/tests/test_gfx2_i1.py', 'tools/tests/gfx2_i1_host.c', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'tools/tests/kernel_layout.py', 'tools/tests/paging_source.py',
            'tools/tests/pgalloc_host_fixture.h', 'tools/tests/host_arch/**', 'gfx/v2/**',
            'gfx/sync/**', 'gfx/palette*', 'gfx/gfx.h', 'gfx/gfx_policy.h', 'mm/**', 'include/**',
            'sdk/include/**', 'lib/kstring*', 'lib/dma_contract.c', 'lib/divdi3.c', 'kernel/module/**', 'kernel/time/cpu_calibrate.h',
            'gfx/gfx_core.c', 'userland/tests/gfx2_i1.c', 'exec/**', 'build/os32.ld', 'platform/pc98/kernel.c'],
    kind='characterization',
)

def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_check_equiv.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
