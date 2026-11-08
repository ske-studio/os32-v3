#!/usr/bin/env python3
CHECK = dict(name='kapi-ranges-host', tier=1,
    covers=['include/ksetjmp.h', 'arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'net/host_mailbox.c', 'net/host_mailbox.h', 'kapi/kapi_host_notice.c', 'kapi/kapi_host_notice.h', 'lib/sqlite3/generated/*', 'exec/access_walk.c', 'exec/exec.c', 'exec/appslot.h', 'include/config.h', 'exec/redir_access.c', 'exec/ring3_str.c', 'kapi/kapi_generated.c', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sdk/kapi.json', 'tools/tests/access_walk_host.c', 'tools/tests/host32.py', 'tools/tests/kapi_ranges_host.c', 'tools/tests/mutpar.py', 'tools/tests/test_access_walk.py', 'tools/tests/kernel_layout.py', 'tools/tests/test_kapi_out.py', 'tools/tests/test_kapi_ranges.py', 'fs/vfs.c', 'fs/vfs.h', 'kapi/kapi_sys.c', 'sdk/include/os32/*.h', 'include/ide_contract.h', 'include/platform_device.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'tools/tests/paging_source.py', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=4.1)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_kapi_ranges.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
