#!/usr/bin/env python3
CHECK = dict(name='access-host', tier=1,
    covers=['sys/include/display_provider.h', 'tools/tests/sqlite_bits_host.h', 'lib/softfloat/**', 'lib/softfloat_bits.c', 'lib/softfloat_bits.h', 'sys/kapi/kapi_print.c', 'sys/include/kapi_call.h', 'sys/arch/x86/kernel/kapi_call.c', 'sys/arch/x86/kernel/syscall_entry.c', 'tools/tests/syscall_source.py', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'tools/np21w_mcp/symbols.py', 'sys/kern/exec/ring3_ls.h', 'sys/platform/pc98/legacy/v86.h', 'sys/kern/exec/exec.c', 'sys/kern/exec/redir_access.c', 'tools/tests/caller_access_host.c', 'tools/tests/mutpar.py', 'tools/tests/test_caller_access.py', 'sys/include/redir_access.h', 'sys/include/kapi_strings.h', 'sys/include/kapi_print.h', 'sdk/include/os32/os32_kapi_slots.h', 'tools/h3_park_resume.py', 'tools/h3_layout.c', 'tools/np21w_mcp/gui.py', 'tools/np21w_mcp/gui_desc.py', 'userland/gshell/src/wm.rs', 'userland/gshell/src/startmenu.rs', 'userland/gshell/src/taskbar.rs', 'tools/np21w_mcp/np21w_client.py', 'tools/tests/host32.py', 'tools/tests/test_host32.py', 'sys/platform/pc98/kdebug/kselftest.c', 'sys/kern/exec/access_walk.c', 'sys/kern/mm/paging.c', 'sys/platform/pc98/platform_memory_desc.h', 'sys/include/platform_memory.h', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shlib.c', 'tools/tests/access_walk_host.c', 'tools/tests/test_access_walk.py', 'tools/tests/kernel_layout.py', 'tools/tests/caller_copy_host.c', 'tools/tests/test_caller_copy.py', 'sys/kern/exec/ring3_str.c', 'sys/kapi/kapi_db.c', 'sys/kapi/kapi_strings.c', 'sys/hal/block/dev.h', 'tools/tests/db_caller_host.c', 'tools/tests/test_db_caller.py', 'sys/kapi/kapi_generated.c', 'sys/fs/vfs.c', 'userland/diag/db_v50_test.c', 'tools/tests/db_group_shim.h', 'sys/kern/exec/surface_query.c', 'tools/tests/test_surface_query.py', 'tools/tests/surface_query_host.c', 'tools/run_host_group.py', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=9.5,
    basis=['KAPI_SPEC §3-3', 'KAPI_SPEC §4'],
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/run_host_group.py', *shlex.split(('--mutate' if ctx.mutate else '')), '--plain', 'tools/tests/test_caller_access.py', '--control', 'tools/tests/test_host32.py', '--host32', 'tools/tests/test_access_walk.py', '--host32', 'tools/tests/test_caller_copy.py', '--host32', 'tools/tests/test_db_caller.py', '--host32', 'tools/tests/test_surface_query.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
