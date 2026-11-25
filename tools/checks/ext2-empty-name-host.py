#!/usr/bin/env python3
CHECK = dict(name='ext2-empty-name-host', tier=1,
    covers=['sys/include/ext2_medium_contract.h', 'sys/platform/pc98/storage/ext2_medium_ide.c', 'fs/**', 'tools/tests/host32.py', 'build/core.yaml', 'sys/fs/pc98pt.c', 'sys/fs/ext2/ext2_dir.c', 'sys/fs/ext2/ext2_file.c', 'sys/fs/ext2/ext2_vfs.c', 'sys/fs/vfs.c', 'tools/deploy_manifests.py', 'tools/mkpkg.py', 'tools/tests/ext2_empty_name_host.c', 'tools/tests/test_ext2_empty_name.py', 'userland/deploy.yaml', 'sys/fs/ext2/ext2_fmt.c', 'sys/fs/ext2/ext2_inode.c', 'sys/fs/ext2/ext2_layout.c', 'sys/fs/ext2/ext2_super.c', 'sys/fs/vfs_fd.c', 'tools/tests/input_fixture_host.h', 'sys/hal/input/input_service.c', 'sys/hal/input/pointer_service.h', 'sys/hal/input/input_service.h', 'sys/include/kbd.h', 'sys/fs/vfs_device.h', 'sys/platform/pc98/boot_names.h', 'sys/platform/pc98/boot_names.c', 'sys/dev/ata/ide.h', 'sys/include/platform_device.h', 'sys/include/ide_contract.h', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=6.0,
    kind='characterization',
)


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
