#!/usr/bin/env python3
CHECK = dict(name='r1-fixture-host', tier=1,
    covers=['kernel/irq/tick.c', 'kernel/irq/tick.h', 'platform/pc98/tick_order.h', 'kdebug/selftest_lease.c', 'include/kselftest_probe.h', 'kdebug/selftest_internal.h', 'tools/tests/lease_selftest_host.h', 'tools/tests/font_guard_inactive.h', 'kernel/v86/v86_module.h', 'kernel/r1_fixture.c', 'kernel/r1_fixture.h', 'arch/x86/kernel/isr_handlers.c', 'kernel/v86/v86_mem.c', 'kernel/v86/v86_mem.h', 'mm/pgalloc.c', 'mm/pgalloc.h', 'mm/physmem.c', 'mm/physmem.h', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'exec/exec.c', 'exec/exec_image.inc', 'exec/exec_argv.inc', 'tools/r1_manifest.py', 'tools/nhd_profile.py', 'tools/nhd_deploy.py', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/pc98pt.py', 'tools/np21w_ctl.py', 'tools/tests/r1_fixture_host.c', 'tools/tests/test_r1_fixture.py', 'tools/tests/test_nhd_deploy_profile.py', 'tools/tests/host32.py', 'tools/tests/pgalloc_host_fixture.h', 'include/**', 'sdk/include/os32/**', 'build/core.yaml', 'exec/exec_kapi_init.inc', 'userland/deploy.yaml', 'gfx/gfx_core.c', 'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pegc.c', 'drivers/video/cirrus/backend_cirrus.c', 'drivers/video/cirrus/cirrus_identify.c', 'gfx/gfx_vram.c', 'gfx/gfx_scroll.c', 'exec/surface_query.c', 'exec/lease.c', 'exec/access_walk.c', 'exec/redir_access.c', 'mm/shlib.c', 'mm/paging.c', 'kernel/sys.c', 'userland/lib/gfx/libos32gfx_core.c', 'userland/lib/gfx/draw/gfx_draw.c', 'userland/lib/gfx/draw/gfx_blt.c', 'userland/lib/gfx/draw/gfx_surface.c', 'userland/lib/gfx/draw/gfx_sprite.c', 'userland/lib/gfx/draw/gfx_rotate.c', 'userland/lib/gfx/draw/gfx_raster.c', 'userland/lib/gfx/draw/gfx_dump.c', 'userland/lib/gfx/geom/gfx_fill.c', 'userland/lib/gfx/geom/gfx_circle.c', 'userland/lib/gfx/geom/gfx_bezier.c', 'userland/lib/gfx/libos32gfx.h', 'userland/lib/gfx/libgfx_internal.h', 'userland/lib/gfx/libgfx_attach_internal.h', 'userland/lib/math/libos32math.h', 'lib/utf8.c', 'lib/utf8.h', 'exec/appslot.c', 'exec/appslot.h', 'exec/owner_diag.h', 'tools/tests/r1_recovery.py', 'tools/tests/r1_recovery_host.c', 'tools/tests/r1_lease_host.c', 'tools/tests/gfx_attach_host.c', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/test_gfx_attach.py', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/kernel_layout.py', 'tools/tests/host_arch/**', 'platform/pc98/platform_io.h', 'tools/tests/mutpar.py', 'tools/gen_deploy_set.py', 'kernel/console/display_restore.h', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=78.787468)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_r1_fixture.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    env = dict(base_env)
    argv = ['python3', 'tools/tests/test_nhd_deploy_profile.py', '--set', *shlex.split(ctx.make_vars['BUILD_OUT'] + '/deploy-set.json'), *shlex.split(('--mutate' if ctx.make_vars.get('MUTATE', '1' if ctx.mutate else '0') == '1' else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
