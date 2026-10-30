#!/usr/bin/env python3
CHECK = dict(name='ver-format-host', tier=1,
    covers=['kernel/ver_format.c', 'kernel/ver_info.h', 'kernel/module/module.c', 'kernel/module/module.h', 'kernel/sqlite_engine.h', 'arch/x86/kernel/idt.h', 'include/input_service.h', 'kernel/input_service.c', 'platform/pc98/d2_bind.c', 'include/bootinfo.h', 'include/config.h', 'include/types.h', 'lib/kstring.h', 'lib/kstring_asm.asm', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ver_info.h', 'tools/tests/ver_format_host.c', 'tools/tests/test_ver_format.py', 'tools/tests/ver_test_utils.py', 'tools/tests/host32.py', 'build/checks.d/check-ver-format-host.mk', 'tools/check_map.d/check-ver-format-host.yaml', 'tools/tests/kernel_includes.py'],
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
        argv = ['python3', '-B', 'tools/tests/test_ver_format.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
