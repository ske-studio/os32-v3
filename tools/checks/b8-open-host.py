#!/usr/bin/env python3
CHECK = dict(name='b8-open-host', tier=1,
    covers=['sys/include/ext2_medium_contract.h', 'sys/platform/pc98/storage/ext2_medium_ide.c', 'sys/fs/ext2/ext2_time.c', 'tools/tests/host32.py', 'sys/fs/pc98pt.c', 'sys/fs/ext2/ext2_dir.c', 'sys/fs/ext2/ext2_file.c', 'sys/fs/ext2/ext2_fmt.c', 'fs/ext2/ext2_fmt_*.inc', 'sys/fs/ext2/ext2_inode.c', 'sys/fs/ext2/ext2_super.c', 'sys/fs/ext2/ext2_vfs.c', 'sys/dev/hostdrv/hostdrvfs.c', 'sys/fs/vfs.c', 'sys/fs/vfs_fd.c', 'lib/kutf16.c', 'tools/tests/b8_e2fsck.py', 'tools/tests/b8_hostdrv_host.c', 'tools/tests/b8_open_host.c', 'tools/tests/test_b8_hostdrv.py', 'tools/tests/test_b8_open.py', 'sys/fs/ext2/ext2_layout.c', 'tools/tests/input_fixture_host.h', 'sys/hal/input/input_service.c', 'sys/hal/input/pointer_service.h', 'sys/hal/input/input_service.h', 'sys/include/kbd.h', 'sys/fs/vfs_device.h', 'sys/platform/pc98/boot_names.h', 'sys/platform/pc98/boot_names.c', 'sys/dev/ata/ide.h', 'sys/include/platform_device.h', 'sys/include/ide_contract.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=42.3,
    kind='characterization',
    env_limit='hostでext2/HostDrvの失敗分岐を確認するが、実媒体障害と実ハイパーコールの失敗は再現しない。',
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_b8_open.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_b8_hostdrv.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
