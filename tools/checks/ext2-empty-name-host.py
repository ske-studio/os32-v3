#!/usr/bin/env python3
CHECK = dict(name='ext2-empty-name-host', tier=1,
    covers=['fs/**', 'tools/tests/host32.py', 'build/core.yaml', 'drivers/block/pc98pt.c', 'fs/ext2/ext2_dir.c', 'fs/ext2/ext2_file.c', 'fs/ext2/ext2_vfs.c', 'fs/vfs.c', 'tools/deploy_manifests.py', 'tools/mkpkg.py', 'tools/tests/ext2_empty_name_host.c', 'tools/tests/test_ext2_empty_name.py', 'userland/deploy.yaml', 'fs/ext2/ext2_fmt.c', 'fs/ext2/ext2_inode.c', 'fs/ext2/ext2_layout.c', 'fs/ext2/ext2_super.c', 'fs/vfs_fd.c', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'include/platform_device.h', 'include/ide_contract.h', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=6.450666)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_ext2_empty_name.py', '--target', *shlex.split(('--mutants' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
