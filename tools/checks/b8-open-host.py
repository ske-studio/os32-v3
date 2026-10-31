#!/usr/bin/env python3
CHECK = dict(name='b8-open-host', tier=1,
    covers=['tools/tests/host32.py', 'drivers/block/pc98pt.c', 'fs/ext2/ext2_dir.c', 'fs/ext2/ext2_file.c', 'fs/ext2/ext2_fmt.c', 'fs/ext2/ext2_fmt_*.inc', 'fs/ext2/ext2_inode.c', 'fs/ext2/ext2_super.c', 'fs/ext2/ext2_vfs.c', 'fs/hostdrv/hostdrvfs.c', 'fs/vfs.c', 'fs/vfs_fd.c', 'lib/kutf16.c', 'tools/tests/b8_e2fsck.py', 'tools/tests/b8_hostdrv_host.c', 'tools/tests/b8_open_host.c', 'tools/tests/test_b8_hostdrv.py', 'tools/tests/test_b8_open.py', 'fs/ext2/ext2_layout.c', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'include/platform_device.h', 'include/ide_contract.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=192.61074)


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
