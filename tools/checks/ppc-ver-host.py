#!/usr/bin/env python3
CHECK = dict(name='ppc-ver-host', tier=1,
    covers=['tools/tests/test_ppc_ver.py', 'tools/tests/ppc_ver_host.c',
            'tools/tests/ver_test_utils.py', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'platform/powermac/ver_backend.c', 'platform/powermac/platform_ver.h',
            'kernel/ver_format.c', 'kernel/ver_info.h', 'kernel/sqlite_engine.h',
            'arch/ppc/*.h', 'arch/x86/*.h', 'include/*.h', 'lib/kstring*', 'sdk/include/os32/*.h',
            'build/config.mk'],
    needs=[], budget=4, basis=['docs/design/kernel/ver_format.md PowerMac の表示'],
)


def run(ctx) -> int:
    import os
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_ppc_ver.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
