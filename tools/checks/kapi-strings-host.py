#!/usr/bin/env python3
CHECK = dict(name='kapi-strings-host', tier=1,
    covers=['kapi/kapi_strings.c', 'include/kapi_strings.h', 'exec/exec.c', 'exec/exec_image.inc', 'exec/exec_kapi_init.inc', 'exec/ring3_ls.c', 'exec/redir_access.c', 'exec/access_walk.c', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'sdk/kapi.json', 'userland/tests/kstr_boundary.c', 'userland/tests/kstr_v86.c', 'build/app.conf', 'tools/tests/test_kapi_strings.py', 'tools/tests/kapi_strings_host.c', 'tools/tests/test_access_walk.py', 'tools/tests/access_walk_host.c', 'tools/tests/pgalloc_host_fixture.h', 'tools/tests/kernel_layout.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=1.997898)


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
