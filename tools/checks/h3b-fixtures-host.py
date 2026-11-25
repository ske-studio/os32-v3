#!/usr/bin/env python3
CHECK = dict(name='h3b-fixtures-host', tier=1,
    covers=['sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/hal/gfx/gfx_emergency.c', 'sys/hal/gfx/gfx_emergency.h', 'tools/tests/paging_source.py', 'build/core.yaml', 'sys/dev/block/loop_dev.c', 'sys/kern/exec/access_walk.c', 'sys/kern/exec/lease.c', 'sys/kern/exec/redir_access.c', 'sys/kern/exec/surface_query.c', 'sys/fs/pipe_buffer.c', 'sys/dev/video/cirrus/backend_cirrus.c', 'sys/platform/pc98/video/cirrus/cirrus_identify.c', 'sys/platform/pc98/video/backend_pc98.c', 'sys/platform/pc98/video/backend_pegc.c', 'sys/hal/gfx/gfx_core.c', 'sys/hal/gfx/gfx_scroll.c', 'sys/platform/pc98/gfx_scroll.c', 'sys/platform/pc98/gfx_init.c', 'sys/platform/pc98/gfx_selector.c', 'sys/platform/pc98/gfx_surfaces.c', 'sys/kern/console/display_restore.h', 'sys/platform/pc98/video/gfx_vram.c', 'sys/hal/gfx/gfx_dirty.c', 'sys/hal/gfx/gfx_present.c', 'sys/kern/console/console.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shlib.c', 'sys/kern/sys.c', 'sys/platform/pc98/legacy/v86.c', 'sys/platform/pc98/legacy/v86_mem.c', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/gen_v86_int80.py', 'tools/hostdrv_deploy.py', 'tools/nhd_deploy.py', 'tools/np21w_ctl.py', 'tools/nhd_profile.py', 'tools/pc98pt.py', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/h3b_fixtures_host.c', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/shutdown_probe_host.c', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/kernel_layout.py', 'tools/tests/test_h3b_fixtures.py', 'tools/tests/test_shutdown_probe.py', 'userland/deploy.yaml', 'userland/diag/audit_test.c', 'userland/diag/kout_test.c', 'userland/diag/pipe_owner_test.c', 'userland/diag/shutdown_probe.c', 'tools/gen_deploy_set.py', 'sys/kern/console/console_backend.h', 'sys/platform/pc98/console.c', 'sys/include/console_attr.h', 'tools/tests/kernel_includes.py', 'sys/dev/block/loop_dev_format.inc', 'sys/dev/block/loop_dev_block_io.inc', 'sys/dev/block/loop_dev_chs_io.inc', 'sys/dev/block/loop_dev.h', 'sys/hal/block/dev.h', 'sys/fs/vfs.h', 'sys/include/types.h', 'sys/include/kprintf.h', 'lib/le_internal.h', 'tools/tests/loop_dev_host.c', 'userland/shell/cmd_mnt.c', 'userland/shell/cmd_fs_shared.h', 'userland/shell/shell.h', 'sys/include/config.h', 'sdk/include/os32/**', 'sys/include/loop_contract.h'],
    needs=['make:all'], budget=6.4,
    basis=['KAPI_SPEC §4-8', '[D1]', '[D2]'],
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_h3b_fixtures.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_shutdown_probe.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
