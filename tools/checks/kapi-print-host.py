#!/usr/bin/env python3
CHECK = dict(name='kapi-print-host', tier=1,
    covers=['sys/kapi/kapi_print.c', 'sys/include/kapi_call.h', 'sys/arch/x86/kernel/kapi_call.c', 'sys/arch/x86/kernel/syscall_entry.c', 'sys/include/kapi_print.h', 'sys/include/kprintf.h', 'sys/kern/kprintf.c', 'sys/kern/kprintf_attr.c', 'sys/include/console_attr.h', 'sys/platform/pc98/console_attr.c', 'sys/kern/exec/exec.c', 'sys/kern/exec/redir_access.c', 'sys/kern/exec/access_walk.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shlib.c', 'sys/platform/pc98/kdebug/kselftest.c', 'kdebug/kselftest/**', 'tools/tests/kapi_print_host.c', 'tools/tests/test_kapi_print.py', 'tools/tests/test_access_walk.py', 'tools/tests/access_walk_host.c', 'tools/tests/pgalloc_host_fixture.h', 'tools/tests/kernel_layout.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'sdk/kapi.json', 'docs/KAPI_SPEC.md', 'userland/diag/kprint_boundary.c', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=3.4,
    basis=['KAPI_SPEC §kprintf の CPL3 入力境界 (T4b-8)'],
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
        argv = ['python3', '-B', 'tools/tests/test_kapi_print.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
