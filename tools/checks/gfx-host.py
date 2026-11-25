#!/usr/bin/env python3
CHECK = dict(name='gfx-host', tier=1,
    covers=['sys/include/display_provider.h', 'tools/tests/gfx2_core_fake.h', 'tools/tests/gfx2_bridge_fake.h', 'sys/include/copy_units.h', 'sys/arch/x86/arch_copy.h', 'sys/arch/ppc/arch_copy.h', 'tools/tests/host_arch/arch_copy.h', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/hal/gfx/gfx_emergency.c', 'sys/hal/gfx/gfx_emergency.h', 'tools/tests/paging_source.py', 'sys/include/gfx_format.h', 'sys/kern/exec/access_walk.c', 'sys/kern/exec/lease.c', 'sys/kern/exec/redir_access.c', 'sys/kern/exec/surface_query.c', 'sys/dev/video/cirrus/backend_cirrus.c', 'sys/platform/pc98/video/cirrus/cirrus_identify.c', 'sys/platform/pc98/video/backend_pc98.c', 'sys/platform/pc98/video/backend_pegc.c', 'sys/hal/gfx/gfx_core.c', 'sys/hal/gfx/palette.c', 'sys/hal/gfx/gfx_scroll.c', 'sys/platform/pc98/gfx_scroll.c', 'sys/platform/pc98/gfx_init.c', 'sys/platform/pc98/gfx_selector.c', 'sys/platform/pc98/gfx_surfaces.c', 'sys/kern/console/display_restore.h', 'sys/platform/pc98/video/gfx_vram.c', 'sys/hal/gfx/gfx_dirty.c', 'sys/hal/gfx/gfx_present.c', 'sys/kern/mm/paging.c', 'sys/platform/pc98/platform_memory_desc.h', 'sys/include/platform_memory.h', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shlib.c', 'sys/kern/sys.c', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/kernel_layout.py', 'lib/serial_diag.h', 'tools/tests/serial_diag_host.h', 'sys/hal/gfx/gfx_internal.h', 'tools/tests/gfx_reinit_host.c', 'tools/tests/test_gfx_reinit.py', 'lib/utf8.c', 'tools/tests/gfx_attach_host.c', 'tools/tests/test_gfx_attach.py', 'userland/lib/gfx/**', 'tools/tests/test_gfx_reattach.py', 'tools/tests/gfx_reattach_host.c', 'tools/run_host_group.py', 'tools/tests/kernel_includes.py', 'tools/tests/recover_gf_dev_stub.c', 'sys/hal/gfx/gfx_core_bridge.inc', 'sys/hal/gfx/gfx_core_framebuffer.inc', 'sys/hal/gfx/gfx_policy.h', 'tools/tests/test_gfx_map_policy.py'],
    needs=['make:all'], budget=13.2,
    basis=['GFX_ACCEL_HAL_DESIGN §3', 'HAL_V2_DESIGN §7-4', 'GFX_ACCEL_HAL_DESIGN §2', 'GFX_ACCEL_HAL_DESIGN §3-4'],
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/run_host_group.py', *shlex.split(('--mutate' if ctx.mutate else '')), '--host32', 'tools/tests/test_gfx_kernel_fb.py', '--host32', 'tools/tests/test_gfx_reinit.py', '--host32', 'tools/tests/test_gfx_attach.py', '--host32', 'tools/tests/test_gfx_reattach.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
