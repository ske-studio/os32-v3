#!/usr/bin/env python3
CHECK = dict(name='ver-services-host', tier=1,
    covers=['kernel/sqlite_engine.c', 'kernel/sqlite_engine.h', 'tools/tests/ver_services_host.c', 'lib/kstring_asm.asm', 'include/types.h', 'tools/tests/test_ver_services.py', 'tools/tests/ver_test_utils.py', 'tools/tests/host32.py', 'build/checks.d/check-ver-services-host.mk', 'tools/check_map.d/check-ver-services-host.yaml', 'tools/tests/kernel_includes.py'],
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
        argv = ['python3', '-B', 'tools/tests/test_ver_services.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
