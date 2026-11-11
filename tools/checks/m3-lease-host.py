#!/usr/bin/env python3
CHECK = dict(name='m3-lease-host', tier=1,
    covers=['kdebug/selftest_lease.c', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'include/memmap.h', 'exec/lease.c', 'gfx/v2/gfx2_int.h',
            'tools/tests/m3_lease_host.c', 'tools/tests/test_m3_lease.py',
            'tools/tests/host32.py', 'tools/tests/paging_source.py',
            'tools/tests/kernel_includes.py', 'tools/tests/kernel_layout.py'],
    needs=['make:all'], budget=30)

def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_m3_lease.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
