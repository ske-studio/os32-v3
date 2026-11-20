#!/usr/bin/env python3
CHECK = dict(name='fep-standard-host', tier=1,
    covers=['include/ext2_medium_contract.h', 'fs/ext2/ext2_medium_ide.c', 'tools/tests/sqlite_cc.py', 'fs/ext2/ext2_fmt_*.inc', 'tools/tests/test_fep_standard.py', 'tools/tests/fep_standard_host.c', 'tools/tests/fep_budget_linux.c', 'tools/tests/hdd_pt_fake.h', 'tools/tests/host32.py', 'kernel/ime/ime_dict.c', 'kernel/ime/ime_conn.c', 'kernel/ime/ime_user_store.c', 'kernel/ime/ime_store.h', 'tools/tests/fep_store_host.h', 'lib/kstring.h', 'kernel/ime/ime.h', 'fs/vfs.c', 'fs/vfs_fd.c', 'fs/vfs.h', 'fs/ext2/ext2*.c', 'fs/ext2/ext2*.h', 'fs/pc98pt.c', 'fs/pc98pt.h', 'kernel/dev/dev.h', 'lib/sqlite3/**', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'include/platform_device.h', 'include/ide_contract.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=8.7,
    basis=['docs/design/kernel/sqlite_engine.md 予算', 'docs/design/kernel/ime/ime_dict.md 検索の合成'],
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
        argv = ['python3', '-B', 'tools/tests/test_fep_standard.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
