#!/usr/bin/env python3
CHECK = dict(name='fep-worst-load-host', tier=1,
    covers=['tools/tests/sqlite_cc.py', 'tools/tests/test_fep_budget.py', 'tools/tests/fep_budget_host.c',
            'tools/tests/fep_budget_linux.c', 'tools/tests/ime_dict_host.c',
            'tools/tests/vfs_fd_sqlite_host.c', 'tools/tests/sqlite_groups_backend.h',
            'tools/tests/fep_store_host.h', 'tools/tests/input_fixture_host.h',
            'tools/tests/serial_emergency_host.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'tools/tests/host_arch/**',
            'tools/fep_budget_capture.py', 'tools/fep_to_sqlite.py',
            'sys/kern/ime/ime_dict.c', 'sys/kern/ime/ime_conn.c',
            'sys/kern/ime/ime_user_store.c', 'sys/kern/ime/ime_store.h',
            'sys/kern/ime/ime.h', 'sys/hal/input/input_service.c', 'sys/kern/serial_diag.c',
            'sys/fs/vfs_fd.c', 'sys/fs/vfs.h', 'lib/sqlite3/**', 'sys/kern/kstring.h',
            'lib/serial_diag.h', 'include/**',
            'sdk/include/os32/os32_kapi_shared.h', 'assets/ipadic/**',
            'assets/joyo_kanji.txt'],
    needs=[], budget=65.7,
    basis=['docs/design/kernel/sqlite_engine.md 予算'],
    env_limit='合成辞書の容量上限を確認するが、試験用SQLite直結でmodule READY・outer thunk・boot fault経路を迂回するため実起動時の接続障害は保証しない。',
)


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
