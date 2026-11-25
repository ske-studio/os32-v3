#!/usr/bin/env python3
CHECK = dict(name='hdd-stage2-host', tier=1,
    covers=['sys/platform/pc98/platform_install_layout.h', 'sys/include/platform_install_boot.h', 'sys/include/ide_contract.h', 'sys/platform/pc98/install_disk.c', 'sys/platform/pc98/install_geometry.h', 'sys/platform/pc98/install_plan.c', 'sys/platform/pc98/install_target.c', 'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/ext2_medium_host.c', 'sys/include/loop_contract.h', 'sys/include/ext2_medium_contract.h', 'sys/platform/pc98/storage/ext2_medium_ide.c', 'fs/**', 'tools/gen_memmap.py', 'sys/include/memmap.h', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'boot/boot_hdd.asm', 'boot/boot_main.c', 'boot/boot_debug.c', 'boot/ext2_mini.c', 'build/core.yaml', 'sys/dev/ata/ide_addr.c', 'sys/fs/pc98pt.c', 'sys/kern/exec/exec.c', 'sys/fs/ext2/ext2_dir.c', 'sys/fs/ext2/ext2_file.c', 'sys/fs/ext2/ext2_fmt.c', 'sys/fs/ext2/ext2_inode.c', 'sys/fs/ext2/ext2_layout.c', 'sys/fs/ext2/ext2_super.c', 'sys/kapi/kapi_generated.c', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/nhd_deploy.py', 'tools/np21w_ctl.py', 'tools/pc98pt.py', 'tools/tests/cdinst_host.c', 'tools/tests/ext2_mini_host.c', 'tools/tests/ext2_part_host.c', 'tools/tests/hdd_stage1_host.c', 'tools/tests/hdd_stage2_host.c', 'tools/tests/install_fresh_host.c', 'tools/tests/test_hdd_stage1.py', 'tools/tests/test_hdd_stage2.py', 'userland/deploy.yaml', 'userland/lib/rt/pkg.c', 'userland/shell/hdprep_plan.c', 'userland/system/cdinst.c', 'userland/system/inst_disk.c', 'userland/system/inst_hdd.c', 'userland/system/install.c', 'sdk/kapi.json', 'tools/nhd_profile.py', 'tools/gen_deploy_set.py', 'boot/loader_fat_new.asm', 'build/os32.ld', 'sys/fs/fd_redirect.c', 'sys/fs/pipe_buffer.c', 'sys/arch/x86/kernel/kentry.asm', 'sys/kern/mm/shm.c', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/gen_memmap.py', 'tools/mkshlib.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=9.7,
    kind='characterization',
)


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
