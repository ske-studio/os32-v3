#!/usr/bin/env python3
CHECK = dict(name='dep-dir-d3-host', tier=1,
    covers=['fs/vfs.h', 'fs/vfs.c', 'fs/ext2/ext2_vfs.c', 'init/kernel.c', 'include/input_service.h', 'kernel/input_service.c', 'kernel/ver_format.c', 'tools/tests/test_dep_dir_d3.py', 'tools/tests/dep_dir_d3_host.c', 'tools/tests/vfs_mount_dev_host.c', 'tools/tests/test_vfs_mount_dev.py', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'include/bootinfo.h', 'init/bootinfo.c', 'init/bootinfo_check.c', 'platform/pc98/boot_disk.c', 'platform/pc98/boot_disk.h', 'tools/tests/dep_dir_d3_disk_host.c', 'tools/tests/test_dep_dir_d3_disk.py', 'drivers/ata/ide_addr.h', 'sdk/include/os32/os32_kapi_shared.h', 'include/build_id.h', 'include/memmap.h', 'lib/kstring.h', 'tools/tests/host32.py', 'init/boot_disk.h', 'init/boot_platform.h', 'init/boot_platform.c', 'init/boot_display.h', 'init/boot_display.c', 'platform/pc98/boot.h', 'platform/pc98/boot.c', 'tools/tests/test_dep_dir_d3_boot.py', 'tools/tests/dep_dir_d3_boot_host.c', 'include/platform_device.h', 'include/ide_contract.h', 'include/platform_emergency.h', 'platform/pc98/emergency.c', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc'],
    needs=[], budget=60.0)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_d3.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_d3_disk.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_d3_boot.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
