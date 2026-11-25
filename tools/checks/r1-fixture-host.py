#!/usr/bin/env python3
CHECK = dict(name='r1-fixture-host', tier=1,
    covers=['sys/arch/x86/arch_jmp_storage.h', 'sys/platform/pc98/legacy/lowmap.c', 'sys/kern/mm/paging_lowmap.h', 'sys/kern/mm/paging_lowmap.inc', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/include/fault_fixture.h', 'sys/arch/x86/kernel/fault_fixture.c', 'sys/hal/gfx/gfx_emergency.c', 'sys/hal/gfx/gfx_emergency.h', 'sys/kern/irq/tick.c', 'sys/kern/irq/tick.h', 'sys/platform/pc98/tick_order.h', 'sys/arch/x86/kdebug/selftest_lease.c', 'sys/include/kselftest_probe.h', 'sys/kern/kdebug/selftest_internal.h', 'tools/tests/lease_selftest_host.h', 'tools/tests/font_guard_inactive.h', 'sys/platform/pc98/legacy/v86_module.h', 'sys/kern/r1_fixture.c', 'sys/kern/r1_fixture.h', 'sys/arch/x86/kernel/isr_handlers.c', 'sys/platform/pc98/legacy/v86_mem.c', 'sys/platform/pc98/legacy/v86_mem.h', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/pgalloc.h', 'sys/kern/mm/physmem.c', 'sys/kern/mm/physmem.h', 'sys/platform/pc98/kdebug/kselftest.c', 'kdebug/kselftest/**', 'sys/kern/exec/exec.c', 'tools/r1_manifest.py', 'tools/nhd_profile.py', 'tools/nhd_deploy.py', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/pc98pt.py', 'tools/np21w_ctl.py', 'tools/tests/r1_fixture_host.c', 'tools/tests/test_r1_fixture.py', 'tools/tests/test_nhd_deploy_profile.py', 'tools/tests/host32.py', 'tools/tests/pgalloc_host_fixture.h', 'include/**', 'sdk/include/os32/**', 'build/core.yaml', 'userland/deploy.yaml', 'sys/hal/gfx/gfx_core.c', 'sys/platform/pc98/video/backend_pc98.c', 'sys/platform/pc98/video/backend_pegc.c', 'sys/dev/video/cirrus/backend_cirrus.c', 'sys/platform/pc98/video/cirrus/cirrus_identify.c', 'sys/platform/pc98/video/gfx_vram.c', 'sys/hal/gfx/gfx_dirty.c', 'sys/hal/gfx/gfx_present.c', 'sys/hal/gfx/gfx_scroll.c', 'sys/platform/pc98/gfx_scroll.c', 'sys/platform/pc98/gfx_init.c', 'sys/platform/pc98/gfx_selector.c', 'sys/platform/pc98/gfx_surfaces.c', 'sys/kern/exec/surface_query.c', 'sys/kern/exec/lease.c', 'sys/kern/exec/access_walk.c', 'sys/kern/exec/redir_access.c', 'sys/kern/mm/shlib.c', 'sys/kern/mm/paging.c', 'sys/kern/sys.c', 'userland/lib/gfx/libos32gfx_core.c', 'userland/lib/gfx/draw/gfx_cmd.c', 'userland/lib/gfx/draw/gfx_draw.c', 'userland/lib/gfx/draw/gfx_blt.c', 'userland/lib/gfx/draw/gfx_surface.c', 'userland/lib/gfx/draw/gfx_sprite.c', 'userland/lib/gfx/draw/gfx_rotate.c', 'userland/lib/gfx/draw/gfx_raster.c', 'userland/lib/gfx/draw/gfx_dump.c', 'userland/lib/gfx/geom/gfx_fill.c', 'userland/lib/gfx/geom/gfx_circle.c', 'userland/lib/gfx/geom/gfx_bezier.c', 'userland/lib/gfx/libos32gfx.h', 'userland/lib/gfx/libgfx_internal.h', 'userland/lib/gfx/libgfx_attach_internal.h', 'userland/lib/math/libos32math.h', 'lib/utf8.c', 'lib/utf8.h', 'sys/kern/exec/appslot.c', 'sys/kern/exec/appslot.h', 'sys/kern/exec/owner_diag.h', 'tools/tests/r1_recovery.py', 'tools/tests/r1_recovery_host.c', 'tools/tests/r1_lease_host.c', 'tools/tests/gfx_attach_host.c', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/test_gfx_attach.py', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/kernel_layout.py', 'tools/tests/host_arch/**', 'sys/platform/pc98/platform_io.h', 'tools/tests/mutpar.py', 'tools/gen_deploy_set.py', 'sys/kern/console/display_restore.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/tss.h', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/include/kapi_call_generated.h'],
    needs=['make:all'], budget=140.9,
    basis=['[D1]', '[D2]'],
    env_limit='hostで故障注入と配備防護を確認するが、ud2/stopを置換しており実CPUの例外・停止を保証しない。',
)


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
