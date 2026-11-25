#!/usr/bin/env python3
CHECK = dict(name='tvram-auth-host', tier=1,
    covers=['sys/platform/pc98/legacy/guest.c', 'sys/include/kapi_call.h', 'sys/kern/v86/a0_weak.c', 'tools/tests/av_sound_stubs.c', 'sys/net/host_mailbox.c', 'sys/kapi/kapi_host_notice.c', 'lib/sqlite3/generated/*', 'sys/kern/exec/exec.c', 'sys/kern/exec/exec.h', 'sdk/kapi.json', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sys/kapi/kapi_generated.c', 'tools/tests/test_tvram_auth.py', 'tools/tests/tvram_auth_host.c', 'tools/tests/test_kapi_ranges.py', 'tools/tests/test_access_walk.py', 'tools/tests/kernel_layout.py', 'tools/tests/access_walk_host.c', 'sys/kern/exec/access_walk.c', 'sys/kern/exec/redir_access.c', 'sys/kern/exec/ring3_str.c', 'sys/fs/vfs.c', 'sys/platform/pc98/kdebug/kselftest.c', 'kdebug/kselftest/**', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shlib.c', 'tools/tests/host32.py', 'tools/tests/kapi_ranges_host.c', 'tools/tests/mutpar.py', 'tools/tests/test_kapi_out.py', 'sys/kapi/kapi_sysinfo.c', 'sys/platform/pc98/platform_mem_stat.h', 'sys/platform/powermac/platform_mem_stat.h', 'tools/tests/mem_stat_source.py', 'sdk/include/os32/*.h', 'sys/kern/console/console_backend.h', 'sys/platform/pc98/console.c', 'sys/include/console_attr.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=4.3,
    basis=['KAPI_SPEC §4-8'],
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
        argv = ['python3', '-B', 'tools/tests/test_tvram_auth.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
