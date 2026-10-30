#!/usr/bin/env python3
CHECK = dict(name='h3b-fixtures-host', tier=1,
    covers=['build/core.yaml', 'drivers/block/loop_dev.c', 'exec/access_walk.c', 'exec/lease.c', 'exec/redir_access.c', 'exec/surface_query.c', 'fs/pipe_buffer.c', 'drivers/video/cirrus/backend_cirrus.c', 'drivers/video/cirrus/cirrus_identify.c', 'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pegc.c', 'gfx/gfx_core.c', 'gfx/gfx_scroll.c', 'gfx/gfx_vram.c', 'kernel/console/console.c', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'kernel/sys.c', 'kernel/v86/v86.c', 'kernel/v86/v86_gp_emulate.inc', 'kernel/v86/v86_mem.c', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/gen_v86_int80.py', 'tools/hostdrv_deploy.py', 'tools/nhd_deploy.py', 'tools/nhd_profile.py', 'tools/pc98pt.py', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/h3b_fixtures_host.c', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/shutdown_probe_host.c', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/kernel_layout.py', 'tools/tests/test_h3b_fixtures.py', 'tools/tests/test_shutdown_probe.py', 'userland/deploy.yaml', 'userland/tests/audit_test.c', 'userland/tests/kout_test.c', 'userland/tests/pipe_owner_test.c', 'userland/tests/shutdown_probe.c', 'tools/gen_deploy_set.py', 'kernel/console/console_backend.h', 'platform/pc98/console.c', 'include/console_attr.h', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=4.59903)


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
