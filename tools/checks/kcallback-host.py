#!/usr/bin/env python3
CHECK = dict(name='kcallback-host', tier=1,
    covers=['sys/arch/x86/kernel/kapi_call.c', 'sys/include/kapi_call.h', 'sys/include/syscall_code.h', 'sys/arch/x86/kernel/syscall_code.c', 'userland/lib/rt/ls.c', 'sdk/include/os32/ls.h', 'sys/fs/vfs.c', 'sys/fs/vfs.h', 'sys/fs/ext2/ext2_vfs.c', 'sys/kern/exec/exec.c', 'sys/kern/exec/ring3_ls.c', 'sys/kern/exec/ring3_ls.h', 'sys/arch/x86/kernel/ring3_ls.S', 'sys/kern/exec/ring3_str.h', 'sdk/kapi.json', 'sys/kapi/kapi_generated.c', 'build/kernel.mk', 'tools/tests/kcallback_host.c', 'tools/tests/test_kcallback.py', 'tools/tests/host32.py', 'userland/diag/kcallback_test.c', 'tools/tests/kcallback_result_host.c', 'tools/tests/kernel_layout.py', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=3.7,
    basis=['KAPI_SPEC §4'],
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
        argv = ['python3', '-B', 'tools/tests/test_kcallback.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
