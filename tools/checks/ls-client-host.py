#!/usr/bin/env python3
CHECK = dict(name='ls-client-host', tier=1,
    covers=['sdk/crt/syscalls.c', 'userland/lib/rt/ls.c', 'sdk/include/os32/ls.h', 'sdk/include/os32/os32_ls.h', 'sdk/rust/os32api/src/ls.rs', 'sdk/rust/os32api/src/lib.rs', 'tools/tests/ls_client_host.rs', 'sdk/rust/i686-os32-none.json', 'tools/tests/ls_client_host.c', 'tools/tests/test_ls_client.py', 'tools/tests/host32.py', 'build/checks.d/check-ls-client-host.mk', 'tools/check_map.d/check-ls-client-host.yaml', 'build/sdk/crt/*'],
    needs=['make:all'], budget=2.781058)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_ls_client.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
