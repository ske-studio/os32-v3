#!/usr/bin/env python3
CHECK = dict(name='kapi-bounds-host', tier=3,
    covers=['sys/hal/gfx/gfx_dirty.h', 'tools/tests/gfx2_bridge_fake.h', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/hal/gfx/gfx_emergency.c', 'sys/hal/gfx/gfx_emergency.h', 'tools/tests/paging_source.py', 'sys/include/gfx_format.h', 'sys/dev/sound/fm/fm.c', 'sys/kern/exec/access_walk.c', 'sys/kern/exec/lease.c', 'sys/kern/exec/redir_access.c', 'sys/kern/exec/surface_query.c', 'sys/dev/video/cirrus/backend_cirrus.c', 'sys/platform/pc98/video/cirrus/cirrus_identify.c', 'sys/platform/pc98/video/backend_pc98.c', 'sys/platform/pc98/video/backend_pegc.c', 'sys/hal/gfx/gfx_core.c', 'sys/hal/gfx/gfx_scroll.c', 'sys/platform/pc98/gfx_scroll.c', 'sys/platform/pc98/gfx_init.c', 'sys/platform/pc98/gfx_selector.c', 'sys/platform/pc98/gfx_surfaces.c', 'sys/kern/console/display_restore.h', 'sys/platform/pc98/video/gfx_vram.c', 'sys/hal/gfx/gfx_dirty.c', 'sys/hal/gfx/gfx_present.c', 'sys/kapi/kapi_db.c', 'sys/kern/console/console.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shlib.c', 'sys/kern/sys.c', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/host32.py', 'tools/tests/kapi_bounds_host.c', 'tools/tests/mutpar.py', 'tools/tests/test_gfx_bounds.py', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/kernel_layout.py', 'tools/tests/test_kapi_bounds.py', 'lib/serial_diag.h', 'tools/tests/serial_diag_host.h', 'sys/kern/console/console_backend.h', 'sys/platform/pc98/console.c', 'sys/include/console_attr.h', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=5.8,
    basis=['KAPI_SPEC', '[ABI]'],
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kapi_bounds.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_gfx_bounds.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
