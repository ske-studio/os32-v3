#!/usr/bin/env python3
CHECK = dict(name='hdd-stage2-host', tier=1,
    covers=['fs/**', 'tools/gen_memmap.py', 'include/memmap.h', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'boot/boot_hdd.asm', 'boot/boot_main.c', 'boot/boot_debug.c', 'boot/ext2_mini.c', 'build/core.yaml', 'drivers/ata/ide_addr.c', 'drivers/block/pc98pt.c', 'exec/exec.c', 'fs/ext2/ext2_dir.c', 'fs/ext2/ext2_file.c', 'fs/ext2/ext2_fmt.c', 'fs/ext2/ext2_inode.c', 'fs/ext2/ext2_layout.c', 'fs/ext2/ext2_super.c', 'kapi/kapi_generated.c', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/nhd_deploy.py', 'tools/np21w_ctl.py', 'tools/pc98pt.py', 'tools/tests/cdinst_host.c', 'tools/tests/ext2_mini_host.c', 'tools/tests/ext2_part_host.c', 'tools/tests/hdd_stage1_host.c', 'tools/tests/hdd_stage2_host.c', 'tools/tests/install_fresh_host.c', 'tools/tests/test_hdd_stage1.py', 'tools/tests/test_hdd_stage2.py', 'userland/deploy.yaml', 'userland/lib/rt/pkg.c', 'userland/shell/hdprep_plan.c', 'userland/system/cdinst.c', 'userland/system/inst_disk.c', 'userland/system/inst_hdd.c', 'userland/system/install.c', 'sdk/kapi.json', 'tools/nhd_profile.py', 'tools/gen_deploy_set.py', 'boot/loader_fat_new.asm', 'build/os32.ld', 'fs/fd_redirect.c', 'fs/pipe_buffer.c', 'arch/x86/kernel/kentry.asm', 'mm/shm.c', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/gen_memmap.py', 'tools/mkshlib.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=13.39088)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_hdd_stage2.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
