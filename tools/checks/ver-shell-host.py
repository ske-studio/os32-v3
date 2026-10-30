#!/usr/bin/env python3
CHECK = dict(name='ver-shell-host', tier=4,
    covers=['userland/shell/cmd_base.c', 'sdk/include/os32/os32_ver_info.h', 'include/types.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_kapi_generated.h', 'tools/tests/test_ver_shell.py', 'tools/tests/ver_test_utils.py', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=60.0)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_ver_shell.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
