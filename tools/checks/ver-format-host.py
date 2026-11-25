#!/usr/bin/env python3
CHECK = dict(name='ver-format-host', tier=1,
    covers=['sys/platform/pc98/ver_backend.c', 'sys/platform/pc98/platform_ver.h', 'sys/include/ver_backend_contract.h', 'sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'sys/platform/pc98/bootinfo_wire.h', 'sys/platform/pc98/bootinfo_native.h', 'sys/include/boot_diag.h', 'sys/kern/ver_format.c', 'sys/include/gfx_hal.h', 'sys/include/gfx_mode.h', 'sys/include/gfx_format.h', 'sys/kern/ver_info.h', 'sys/kern/kdebug/kdebug_module.c', 'sys/kern/module/module.h', 'sys/kern/sqlite_engine.h', 'sys/kern/sqlite_engine_body.h', 'sys/arch/x86/kernel/idt.h', 'sys/hal/input/input_service.h', 'sys/hal/input/input_service.c', 'sys/platform/pc98/d2_bind.c', 'sys/include/bootinfo.h', 'sys/include/config.h', 'sys/include/types.h', 'sys/kern/kstring.h', 'sys/kern/kstring_asm.asm', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ver_info.h', 'tools/tests/ver_format_host.c', 'tools/tests/test_ver_format.py', 'tools/tests/ver_test_utils.py', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3.0,
    kind='characterization',
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
        argv = ['python3', '-B', 'tools/tests/test_ver_format.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
