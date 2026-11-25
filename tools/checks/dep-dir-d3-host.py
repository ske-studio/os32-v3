#!/usr/bin/env python3
CHECK = dict(name='dep-dir-d3-host', tier=1,
    covers=['sys/include/machcfg_contract.h', 'sys/include/ext2_medium_contract.h', 'sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'sys/platform/pc98/bootinfo_report.c', 'sys/platform/pc98/bootinfo_geom.c', 'sys/platform/pc98/bootinfo_capture.c', 'sys/platform/pc98/boot_capture.c', 'sys/platform/pc98/bootinfo_wire.h', 'sys/platform/pc98/bootinfo_native.h', 'sys/fs/vfs.h', 'sys/fs/vfs.c', 'sys/fs/ext2/ext2_vfs.c', 'sys/platform/pc98/kernel.c', 'sys/hal/input/input_service.h', 'sys/hal/input/input_service.c', 'sys/kern/ver_format.c', 'tools/tests/test_dep_dir_d3.py', 'tools/tests/dep_dir_d3_host.c', 'tools/tests/vfs_mount_dev_host.c', 'tools/tests/test_vfs_mount_dev.py', 'sys/fs/vfs_device.h', 'sys/platform/pc98/boot_names.h', 'sys/platform/pc98/boot_names.c', 'sys/dev/ata/ide.h', 'sys/include/bootinfo.h', 'sys/kern/init/bootinfo.c', 'sys/platform/pc98/bootinfo_check.c', 'sys/platform/pc98/boot_disk.c', 'sys/platform/pc98/boot_disk.h', 'tools/tests/dep_dir_d3_disk_host.c', 'tools/tests/test_dep_dir_d3_disk.py', 'sys/dev/ata/ide_addr.h', 'sdk/include/os32/os32_kapi_shared.h', 'sys/include/build_id.h', 'sys/include/memmap.h', 'sys/kern/kstring.h', 'tools/tests/host32.py', 'sys/kern/init/boot_disk.h', 'sys/platform/pc98/boot_platform.h', 'sys/platform/pc98/boot_platform.c', 'sys/platform/pc98/boot_display.h', 'sys/platform/pc98/boot_display.c', 'sys/platform/pc98/boot.h', 'sys/platform/pc98/boot.c', 'sys/platform/pc98/usb_probe.h', 'sys/kern/dev/dev_name.h', 'tools/tests/test_dep_dir_d3_boot.py', 'tools/tests/dep_dir_d3_boot_host.c', 'sys/include/platform_device.h', 'sys/include/ide_contract.h', 'sys/include/platform_emergency.h', 'sys/platform/pc98/emergency.c', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'sys/include/kernel_mount_children.h'],
    needs=[], budget=3.4,
    basis=['AC-BOOT-1', 'AC-INPUT-1'],
)


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
