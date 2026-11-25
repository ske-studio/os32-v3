#!/usr/bin/env python3
CHECK = dict(name='m3-lease-host', tier=1,
    covers=['sys/arch/x86/kdebug/selftest_lease.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/include/memmap.h', 'sys/kern/exec/lease.c', 'sys/hal/gfx/v2/gfx2_int.h',
            'tools/tests/m3_lease_host.c', 'tools/tests/test_m3_lease.py',
            'tools/tests/host32.py', 'tools/tests/paging_source.py',
            'tools/tests/kernel_includes.py', 'tools/tests/kernel_layout.py'],
    needs=['make:all'], budget=30,
    basis=['HAL_V2_DESIGN §7-4', 'HAL_V2_DESIGN §11-4'],
)

def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_m3_lease.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
