#!/usr/bin/env python3
CHECK = dict(name='fep-budget-host', tier=1,
    covers=['tools/tests/sqlite_cc.py', 'sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'tools/tests/test_fep_meta_budget.py', 'tools/tests/fep_meta_budget_host.c', 'tools/tests/fep_host_build.py', 'sys/kern/fep_boot.c', 'tools/tests/fep_boundary_runtime.h', 'sys/kern/sqlite_engine.c', 'sys/kern/sqlite_engine.h', 'sys/kern/sqlite_engine_body.h', 'sys/kern/kstring_c.c', 'sys/kern/kstring.c', 'lib/kmath.c', 'lib/divdi3.c', 'include/**', 'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/host_arch/**', 'build/config.mk', 'tools/fep_budget_capture.py', 'tools/tests/test_fep_budget.py', 'tools/tests/fep_budget_host.c', 'tools/tests/fep_budget_linux.c', 'tools/tests/ime_dict_host.c', 'tools/tests/sqlite_groups_backend.h', 'tools/tests/vfs_fd_sqlite_host.c', 'tools/tests/host32.py', 'tools/fep_to_sqlite.py', 'assets/joyo_kanji.txt', 'assets/ipadic/**', 'sys/kern/ime/ime_dict.c', 'sys/kern/ime/ime_conn.c', 'sys/kern/ime/ime_user_store.c', 'sys/kern/ime/ime_store.h', 'tools/tests/fep_store_host.h', 'sys/kern/kstring.h', 'sys/kern/ime/ime.h', 'sys/fs/vfs_fd.c', 'sys/fs/vfs.h', 'lib/sqlite3/**', 'tools/accept/t4a4.md', 'tools/tests/input_fixture_host.h', 'sys/hal/input/input_service.c', 'sys/hal/input/pointer_service.h', 'sys/hal/input/input_service.h', 'sys/include/kbd.h', 'tools/tests/serial_emergency_host.h', 'sys/kern/serial_diag.c', 'sys/include/console_output.h', 'lib/serial_diag.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=27.2,
    basis=['docs/design/kernel/sqlite_engine.md 予算'],
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_fep_meta_budget.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
