#!/usr/bin/env python3
CHECK = dict(name='fep-worst-load-host', tier=1,
    covers=['tools/tests/test_fep_budget.py', 'tools/tests/fep_budget_host.c',
            'tools/tests/fep_budget_linux.c', 'tools/tests/ime_dict_host.c',
            'tools/tests/vfs_fd_sqlite_host.c', 'tools/tests/sqlite_groups_backend.h',
            'tools/tests/fep_store_host.h', 'tools/tests/input_fixture_host.h',
            'tools/tests/serial_emergency_host.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'tools/tests/host_arch/**',
            'tools/fep_budget_capture.py', 'tools/fep_to_sqlite.py',
            'kernel/ime/ime_dict.c', 'kernel/ime/ime_conn.c',
            'kernel/ime/ime_user_store.c', 'kernel/ime/ime_store.h',
            'kernel/ime/ime.h', 'kernel/input_service.c', 'kernel/serial_diag.c',
            'fs/vfs_fd.c', 'fs/vfs.h', 'lib/sqlite3/**', 'lib/kstring.h',
            'lib/serial_diag.h', 'include/**',
            'sdk/include/os32/os32_kapi_shared.h', 'assets/ipadic/**',
            'assets/joyo_kanji.txt'],
    needs=[], budget=65.7)


def run(ctx) -> int:
    import os
    import subprocess
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        argv = ['python3', '-B', 'tools/tests/test_fep_budget.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root,
                             env=dict(os.environ, HOST32_RUNNERS=runner),
                             close_fds=False)
        if rc:
            return rc
    return 0
