#!/usr/bin/env python3
CHECK = dict(name='kapi-strings-host', tier=1,
    covers=['sys/arch/x86/kernel/kapi_call.c', 'sys/include/kapi_call.h', 'sys/include/kapi_call_generated.h', 'sys/arch/x86/v86_call.h', 'sys/arch/x86/kernel/syscall_entry.c', 'sys/kapi/kapi_strings.c', 'sys/include/kapi_strings.h', 'sys/kern/exec/exec.c', 'sys/kern/exec/ring3_ls.c', 'sys/kern/exec/redir_access.c', 'sys/kern/exec/access_walk.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shlib.c', 'sys/platform/pc98/kdebug/kselftest.c', 'kdebug/kselftest/**', 'sdk/kapi.json', 'userland/diag/kstr_boundary.c', 'userland/diag/kstr_v86.c', 'build/app.conf', 'tools/tests/test_kapi_strings.py', 'tools/tests/kapi_strings_host.c', 'tools/tests/test_access_walk.py', 'tools/tests/access_walk_host.c', 'tools/tests/pgalloc_host_fixture.h', 'tools/tests/kernel_layout.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=3.6,
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
        argv = ['python3', '-B', 'tools/tests/test_kapi_strings.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
