#!/usr/bin/env python3
CHECK = dict(name='ver-api-host', tier=1,
    covers=['sdk/kapi.json', 'sdk/gen_kapi.py', 'sdk/kapi_rust_gen.py', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_slots.h', 'sdk/include/os32/os32_generations.h', 'sdk/os32_generations.py', 'sdk/rust/os32api/src/generations.rs', 'sdk/rust/os32api/src/kapi_generated.rs', 'kapi/kapi_generated.c', 'exec/exec_kapi_init.inc', 'kapi/kapi_sys.c', 'kernel/ver_info.h', 'kernel/sqlite_engine.h', 'include/types.h', 'lib/kstring.h', 'lib/kstring_asm.asm', 'tools/tests/test_ver_api.py', 'tools/tests/ver_test_utils.py', 'tools/tests/host32.py', 'build/checks.d/check-ver-api-host.mk', 'tools/check_map.d/check-ver-api-host.yaml', 'tools/tests/kernel_includes.py'],
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
        argv = ['python3', '-B', 'tools/tests/test_ver_api.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
