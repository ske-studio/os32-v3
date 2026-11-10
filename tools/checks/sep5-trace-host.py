#!/usr/bin/env python3
CHECK = dict(name='sep5-trace-host', tier=1,
    covers=['tools/tests/sqlite_cc.py', 'tools/tests/test_sep5_trace.py', 'tools/checks/sep5-trace-host.py',
            'include/diag_stack.h', 'arch/*/arch_diag_stack.h',
            'lib/sqlite3/sqlite3.c', 'lib/sqlite3/sqlite3.h',
            'lib/sqlite3/os32_sqlite_config.h', 'tools/tests/host32.py',
            'tools/tests/test_sqlite_memsys5.py', 'tools/tests/sqlite_memsys5*'],
    needs=[], budget=50)


def run(ctx):
    import os
    import subprocess
    argv = ['python3', '-B', 'tools/tests/test_sep5_trace.py']
    if ctx.mutate:
        argv.append('--mutate')
    if not ctx.runners:
        return 1
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(argv, cwd=ctx.root, env=env)
