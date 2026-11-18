#!/usr/bin/env python3
CHECK = dict(name='crt-exit-host', tier=2,
    covers=['sdk/crt/crt0_c.c', 'sdk/crt/syscalls.c', 'sdk/allocator/**', 'tools/check_nano_inputs.py',
            'sdk/include/**', 'userland/lib/rt/ls.c', 'tools/tests/host32.py',
            'tools/tests/crt_exit_host.c', 'tools/tests/test_crt_exit.py', 'tools/ci/build_cross.sh'],
    needs=['make:sdk/include/os32/os32_unit_stamp.h'], budget=15,
    basis=['sdk/README.md アプリを書くときの約束'],
)


def run(ctx) -> int:
    import os
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_crt_exit.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
