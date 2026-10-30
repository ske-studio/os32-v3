#!/usr/bin/env python3
CHECK = dict(name='fep-budget-host', tier=1,
    covers=['tools/fep_budget_capture.py', 'tools/tests/test_fep_budget.py', 'tools/tests/fep_budget_host.c', 'tools/tests/fep_budget_linux.c', 'tools/tests/ime_dict_host.c', 'tools/tests/sqlite_groups_backend.h', 'tools/tests/vfs_fd_sqlite_host.c', 'tools/tests/host32.py', 'tools/fep_to_sqlite.py', 'assets/joyo_kanji.txt', 'assets/ipadic/**', 'kernel/ime/ime_dict.c', 'kernel/ime/ime.h', 'fs/vfs_fd.c', 'fs/vfs.h', 'lib/sqlite3/**', 'build/checks.d/check-fep-budget-host.mk', 'tools/check_map.d/check-fep-budget-host.yaml', 'tools/accept/t4a4.md', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'tools/tests/serial_emergency_host.h', 'kernel/serial_diag.c', 'include/console_output.h', 'lib/serial_diag.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=32.7422)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_fep_budget.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
