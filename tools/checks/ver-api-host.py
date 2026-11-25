#!/usr/bin/env python3
CHECK = dict(name='ver-api-host', tier=1,
    covers=['sys/platform/pc98/ver_backend.c', 'sys/platform/pc98/platform_ver.h', 'sys/include/ver_backend_contract.h', 'sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'sdk/kapi.json', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sdk/kapi_rust_gen.py', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_slots.h', 'sdk/include/os32/os32_generations.h', 'sdk/os32_generations.py', 'sdk/rust/os32api/src/generations.rs', 'sdk/rust/os32api/src/kapi_generated.rs', 'sys/kapi/kapi_generated.c', 'sys/kapi/kapi_sys.c', 'sys/kapi/kapi_sysinfo.c', 'sys/kapi/kapi_sysinfo.h', 'sys/platform/pc98/platform_build_info.h', 'sys/kern/ver_info.h', 'sys/kern/sqlite_engine.h', 'sys/kern/sqlite_engine_body.h', 'sys/include/types.h', 'sys/kern/kstring.h', 'sys/kern/kstring_asm.asm', 'tools/tests/test_ver_api.py', 'tools/tests/ver_test_utils.py', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3.2,
    basis=['[ABI]', 'KAPI_SPEC §4'],
)


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
