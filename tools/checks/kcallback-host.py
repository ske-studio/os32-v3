#!/usr/bin/env python3
CHECK = dict(name='kcallback-host', tier=1,
    covers=['arch/x86/kernel/kapi_call.c', 'include/kapi_call.h', 'include/syscall_code.h', 'arch/x86/kernel/syscall_code.c', 'userland/lib/rt/ls.c', 'sdk/include/os32/ls.h', 'fs/vfs.c', 'fs/vfs.h', 'fs/ext2/ext2_vfs.c', 'exec/exec.c', 'exec/ring3_ls.c', 'exec/ring3_ls.h', 'arch/x86/kernel/ring3_ls.S', 'exec/ring3_str.h', 'sdk/kapi.json', 'kapi/kapi_generated.c', 'build/kernel.mk', 'tools/tests/kcallback_host.c', 'tools/tests/test_kcallback.py', 'tools/tests/host32.py', 'userland/tests/kcallback_test.c', 'tools/tests/kcallback_result_host.c', 'tools/tests/kernel_layout.py', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=3.7)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_kcallback.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
