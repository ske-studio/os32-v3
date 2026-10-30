#!/usr/bin/env python3
CHECK = dict(name='vfs-mount-dev-host', tier=1,
    covers=['tools/tests/host32.py', 'drivers/block/pc98pt.c', 'fs/ext2/ext2_dir.c', 'fs/ext2/ext2_file.c', 'fs/ext2/ext2_fmt.c', 'fs/ext2/ext2_fmt_*.inc', 'fs/ext2/ext2_inode.c', 'fs/ext2/ext2_super.c', 'fs/ext2/ext2_vfs.c', 'fs/fat/fatfs_vfs.c', 'fs/vfs.c', 'init/bootlog.c', 'lib/microtar/microtar.c', 'tools/tests/ext2_read_bound_host.c', 'tools/tests/ext2_write_io_host.c', 'tools/tests/fatfs_stat_host.c', 'tools/tests/test_ext2_read_bound.py', 'tools/tests/test_ext2_write_io.py', 'tools/tests/test_fatfs_stat.py', 'tools/tests/test_vfs_mount_dev.py', 'tools/tests/vfs_mount_dev_host.c', 'fs/vfs_name_rules.inc', 'fs/ext2/ext2_layout.c', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'include/platform_device.h', 'include/ide_contract.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=6.579096)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_vfs_mount_dev.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_ext2_read_bound.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_ext2_write_io.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_fatfs_stat.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
