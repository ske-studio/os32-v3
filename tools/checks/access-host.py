#!/usr/bin/env python3
CHECK = dict(name='access-host', tier=1,
    covers=['exec/ring3_ls.h', 'kernel/v86/v86.h', 'exec/exec.c', 'exec/redir_access.c', 'tools/tests/caller_access_host.c', 'tools/tests/mutpar.py', 'tools/tests/test_caller_access.py', 'include/redir_access.h', 'include/kapi_strings.h', 'include/kapi_print.h', 'sdk/include/os32/os32_kapi_slots.h', 'tools/h3_park_resume.py', 'tools/h3_layout.c', 'tools/np21w_mcp/gui.py', 'tools/np21w_mcp/gui_desc.py', 'userland/gshell/src/wm.rs', 'userland/gshell/src/startmenu.rs', 'userland/gshell/src/taskbar.rs', 'tools/np21w_mcp/np21w_client.py', 'tools/tests/host32.py', 'tools/tests/test_host32.py', 'kdebug/kselftest.c', 'exec/access_walk.c', 'mm/paging.c', 'platform/pc98/platform_memory_desc.h', 'include/platform_memory.h', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'tools/tests/access_walk_host.c', 'tools/tests/test_access_walk.py', 'tools/tests/kernel_layout.py', 'tools/tests/caller_copy_host.c', 'tools/tests/test_caller_copy.py', 'exec/ring3_str.c', 'kapi/kapi_db.c', 'kapi/kapi_strings.c', 'drivers/base/dev.h', 'tools/tests/db_caller_host.c', 'tools/tests/test_db_caller.py', 'kapi/kapi_generated.c', 'fs/vfs.c', 'userland/tests/db_v50_test.c', 'tools/tests/db_group_shim.h', 'exec/surface_query.c', 'tools/tests/test_surface_query.py', 'tools/tests/surface_query_host.c', 'tools/run_host_group.py', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py'],
    needs=['make:all'], budget=7.263538)


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
