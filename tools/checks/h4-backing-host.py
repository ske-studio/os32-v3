#!/usr/bin/env python3
CHECK = dict(name='h4-backing-host', tier=1,
    covers=['tools/tests/paging_source.py', 'tools/tests/test_h4_backing.py', 'tools/tests/h4_backing_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_layout.py', 'tools/tests/host_arch/**', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'exec/access_walk.c', 'mm/dma_pool.c', 'mm/dma_pool.h', 'mm/dma_pool_math.c', 'drivers/dma/dma8237.h', 'drivers/dma/dma8237_math.c', 'drivers/block/fdc.c', 'drivers/block/fdc.h', 'drivers/block/fdc_decide.c', 'drivers/block/fdc_decide.h', 'fs/vfs.h', 'include/dma_contract.h', 'lib/dma_contract.c', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=5.178602)


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
