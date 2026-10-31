#!/usr/bin/env python3
CHECK = dict(name='kcg-boot-host', tier=1,
    covers=['drivers/video/kcg/kcg.c', 'drivers/video/kcg/kcg.h', 'drivers/video/kcg/kcg_module.h', 'init/kernel.c', 'lib/utf8.c', 'tools/tests/host32.py', 'tools/tests/kcg_boot_host.c', 'tools/tests/test_kcg_boot.py', 'tools/tests/mutpar.py', 'include/kcg_contract.h', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py'],
    needs=[], budget=1.490538)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_kcg_boot.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
