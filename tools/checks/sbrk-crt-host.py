#!/usr/bin/env python3
CHECK = dict(name='sbrk-crt-host', tier=1,
    covers=['sdk/link/app_sys.ld', 'sdk/crt/syscalls.c', 'sdk/allocator/**', 'tools/tests/test_sbrk_crt.py', 'tools/tests/sbrk_crt_host.c', 'tools/tests/host32.py', 'userland/lib/rt/ls.c'],
    needs=[], budget=4.809162)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_sbrk_crt.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
