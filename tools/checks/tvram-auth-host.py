#!/usr/bin/env python3
CHECK = dict(name='tvram-auth-host', tier=1,
    covers=['lib/sqlite3/generated/*', 'exec/exec.c', 'exec/exec.h', 'sdk/kapi.json', 'sdk/gen_kapi.py', 'kapi/kapi_generated.c', 'tools/tests/test_tvram_auth.py', 'tools/tests/tvram_auth_host.c', 'tools/tests/test_kapi_ranges.py', 'tools/tests/test_access_walk.py', 'tools/tests/kernel_layout.py', 'tools/tests/access_walk_host.c', 'exec/access_walk.c', 'exec/redir_access.c', 'exec/ring3_str.c', 'fs/vfs.c', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'tools/tests/host32.py', 'tools/tests/kapi_ranges_host.c', 'tools/tests/mutpar.py', 'tools/tests/test_kapi_out.py', 'kapi/kapi_sys.c', 'sdk/include/os32/*.h', 'kernel/console/console_backend.h', 'platform/pc98/console.c', 'include/console_attr.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py'],
    needs=['make:all'], budget=2.05438)


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
