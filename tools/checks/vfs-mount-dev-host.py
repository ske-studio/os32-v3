#!/usr/bin/env python3
CHECK = dict(name='vfs-mount-dev-host', tier=1,
    covers=['sys/include/ext2_medium_contract.h', 'sys/platform/pc98/storage/ext2_medium_ide.c', 'sys/fs/ext2/ext2_time.c', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'tools/tests/host32.py', 'sys/fs/pc98pt.c', 'sys/fs/ext2/ext2_dir.c', 'sys/fs/ext2/ext2_file.c', 'sys/fs/ext2/ext2_fmt.c', 'fs/ext2/ext2_fmt_*.inc', 'sys/fs/ext2/ext2_inode.c', 'sys/fs/ext2/ext2_super.c', 'sys/fs/ext2/ext2_super_block_io.inc', 'sys/fs/ext2/ext2_vfs.c', 'sys/fs/fat/fatfs_vfs.c', 'sys/fs/vfs.c', 'sys/kern/init/bootlog.c', 'lib/microtar/microtar.c', 'tools/tests/ext2_read_bound_host.c', 'tools/tests/ext2_write_io_host.c', 'tools/tests/fatfs_stat_host.c', 'tools/tests/test_ext2_read_bound.py', 'tools/tests/test_ext2_write_io.py', 'tools/tests/test_fatfs_stat.py', 'tools/tests/test_vfs_mount_dev.py', 'tools/tests/vfs_mount_dev_host.c', 'sys/fs/ext2/ext2_layout.c', 'sys/fs/vfs_device.h', 'sys/platform/pc98/boot_names.h', 'sys/platform/pc98/boot_names.c', 'sys/dev/ata/ide.h', 'sys/include/platform_device.h', 'sys/include/ide_contract.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=6.9,
    kind='characterization',
)


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
