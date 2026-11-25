#!/usr/bin/env python3
CHECK = dict(name='shlib-high-host', tier=1,
    covers=['sys/kern/exec/appslot.c', 'sdk/include/os32/os32_sndl_table.h', 'sys/kern/idle_work.h', 'tools/tests/paging_source.py', 'tools/tests/host32.py', 'tools/tests/test_shlib_high.py', 'tools/tests/shlib_high_host.c', 'sys/kern/mm/shlib.c', 'sys/kern/exec/os32x_hdr.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'tools/tests/kernel_layout.py', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=3.2,
    kind='characterization',
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_shlib_high.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
