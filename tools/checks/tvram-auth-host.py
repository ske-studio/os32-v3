#!/usr/bin/env python3
CHECK = dict(name='tvram-auth-host', tier=1,
    covers=['net/host_mailbox.c', 'kapi/kapi_host_notice.c', 'lib/sqlite3/generated/*', 'exec/exec.c', 'exec/exec.h', 'sdk/kapi.json', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'kapi/kapi_generated.c', 'tools/tests/test_tvram_auth.py', 'tools/tests/tvram_auth_host.c', 'tools/tests/test_kapi_ranges.py', 'tools/tests/test_access_walk.py', 'tools/tests/kernel_layout.py', 'tools/tests/access_walk_host.c', 'exec/access_walk.c', 'exec/redir_access.c', 'exec/ring3_str.c', 'fs/vfs.c', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'tools/tests/host32.py', 'tools/tests/kapi_ranges_host.c', 'tools/tests/mutpar.py', 'tools/tests/test_kapi_out.py', 'kapi/kapi_sys.c', 'sdk/include/os32/*.h', 'kernel/console/console_backend.h', 'platform/pc98/console.c', 'include/console_attr.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'tools/tests/paging_source.py', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=4.3)


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
