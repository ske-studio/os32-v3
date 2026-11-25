#!/usr/bin/env python3
CHECK = dict(name='hdd-stage1-host', tier=1,
    covers=['sys/platform/pc98/platform_install_layout.h', 'sys/platform/pc98/install_plan.c', 'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/ext2_medium_host.c', 'sys/include/loop_contract.h', 'sys/include/ext2_medium_contract.h', 'sys/platform/pc98/storage/ext2_medium_ide.c', 'fs/**', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'boot/boot_main.c', 'boot/boot_debug.c', 'build/core.yaml', 'sys/dev/ata/ide_addr.c', 'sys/fs/pc98pt.c', 'sys/fs/ext2/ext2_dir.c', 'sys/fs/ext2/ext2_file.c', 'sys/fs/ext2/ext2_fmt.c', 'sys/fs/ext2/ext2_inode.c', 'sys/fs/ext2/ext2_layout.c', 'sys/fs/ext2/ext2_super.c', 'sys/fs/ext2/ext2_super_block_io.inc', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/nhd_deploy.py', 'tools/np21w_ctl.py', 'tools/pc98pt.py', 'tools/tests/ext2_part_host.c', 'tools/tests/hdd_stage1_host.c', 'tools/tests/test_hdd_stage1.py', 'userland/deploy.yaml', 'userland/shell/hdprep_plan.c', 'tools/nhd_profile.py', 'tools/gen_deploy_set.py', 'tools/gen_memmap.py', 'sys/include/memmap.h', 'boot/boot_defs.h', 'boot/loader_fat_new.asm', 'build/os32.ld', 'sys/fs/fd_redirect.c', 'sys/fs/pipe_buffer.c', 'sys/arch/x86/kernel/kentry.asm', 'sys/kern/mm/shm.c', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/mkshlib.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'sys/include/ide_contract.h', 'sys/include/platform_device.h', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=7.9,
    kind='characterization',
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_hdd_stage1.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
