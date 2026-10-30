#!/usr/bin/env python3
CHECK = dict(name='sqlite-memsys5-host', tier=1,
    covers=['lib/sqlite3/sqlite3.c', 'lib/sqlite3/sqlite3.h', 'lib/sqlite3/os32_sqlite_config.h', 'tools/tests/sqlite_memsys5_proto.c', 'tools/tests/sqlite_memsys5_proto.h', 'tools/tests/sqlite_memsys5_host.c', 'tools/tests/sqlite_memsys5_start.S', 'tools/tests/sqlite_memsys5_linux.c', 'tools/tests/test_sqlite_memsys5.py', 'tools/tests/sqlite_memsys5_tdd.md', 'tools/tests/host32.py', 'tools/check_map.d/check-sqlite-memsys5-host.yaml', 'build/checks.d/check-sqlite-memsys5-host.mk'],
    needs=[], budget=1.77527)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_sqlite_memsys5.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
