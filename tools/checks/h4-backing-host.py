#!/usr/bin/env python3
CHECK = dict(name='h4-backing-host', tier=1,
    covers=['tools/tests/paging_source.py', 'tools/tests/test_h4_backing.py', 'tools/tests/h4_backing_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_layout.py', 'tools/tests/host_arch/**', 'sys/platform/pc98/kdebug/kselftest.c', 'kdebug/kselftest/**', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/exec/access_walk.c', 'sys/kern/mm/dma_pool.c', 'sys/kern/mm/dma_pool.h', 'sys/kern/mm/dma_pool_math.c', 'sys/dev/dma/dma8237.h', 'sys/dev/dma/dma8237_math.c', 'sys/dev/block/fdc.c', 'sys/dev/block/fdc.h', 'sys/dev/block/fdc_decide.c', 'sys/dev/block/fdc_decide.h', 'sys/fs/vfs.h', 'sys/include/dma_contract.h', 'lib/dma_contract.c', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=7.3,
    kind='characterization',
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_h4_backing.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
