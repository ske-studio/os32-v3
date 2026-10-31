#!/usr/bin/env python3
CHECK = dict(name='kapi-print-host', tier=1,
    covers=['kapi/kapi_print.c', 'include/kapi_print.h', 'include/kprintf.h', 'lib/kprintf.c', 'lib/kprintf_attr.c', 'include/console_attr.h', 'platform/pc98/console_attr.c', 'exec/exec.c', 'exec/exec_image.inc', 'exec/exec_argv.inc', 'exec/exec_kapi_init.inc', 'exec/redir_access.c', 'exec/access_walk.c', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'tools/tests/kapi_print_host.c', 'tools/tests/test_kapi_print.py', 'tools/tests/test_access_walk.py', 'tools/tests/access_walk_host.c', 'tools/tests/pgalloc_host_fixture.h', 'tools/tests/kernel_layout.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'sdk/kapi.json', 'docs/KAPI_SPEC.md', 'userland/tests/kprint_boundary.c', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=2.079208)


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
