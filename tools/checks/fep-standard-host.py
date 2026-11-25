#!/usr/bin/env python3
CHECK = dict(name='fep-standard-host', tier=1,
    covers=['sys/include/ext2_medium_contract.h', 'sys/platform/pc98/storage/ext2_medium_ide.c', 'tools/tests/sqlite_cc.py', 'fs/ext2/ext2_fmt_*.inc', 'tools/tests/test_fep_standard.py', 'tools/tests/fep_standard_host.c', 'tools/tests/fep_budget_linux.c', 'tools/tests/hdd_pt_fake.h', 'tools/tests/host32.py', 'sys/kern/ime/ime_dict.c', 'sys/kern/ime/ime_conn.c', 'sys/kern/ime/ime_user_store.c', 'sys/kern/ime/ime_store.h', 'tools/tests/fep_store_host.h', 'sys/kern/kstring.h', 'sys/kern/ime/ime.h', 'sys/fs/vfs.c', 'sys/fs/vfs_fd.c', 'sys/fs/vfs.h', 'fs/ext2/ext2*.c', 'fs/ext2/ext2*.h', 'sys/fs/pc98pt.c', 'sys/fs/pc98pt.h', 'sys/hal/block/dev.h', 'lib/sqlite3/**', 'tools/tests/input_fixture_host.h', 'sys/hal/input/input_service.c', 'sys/hal/input/pointer_service.h', 'sys/hal/input/input_service.h', 'sys/include/kbd.h', 'sys/fs/vfs_device.h', 'sys/platform/pc98/boot_names.h', 'sys/platform/pc98/boot_names.c', 'sys/dev/ata/ide.h', 'sys/include/platform_device.h', 'sys/include/ide_contract.h', 'tools/tests/kernel_includes.py'],
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
