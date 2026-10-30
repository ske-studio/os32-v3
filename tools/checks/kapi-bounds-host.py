#!/usr/bin/env python3
CHECK = dict(name='kapi-bounds-host', tier=3,
    covers=['include/gfx_format.h', 'drivers/sound/fm/fm.c', 'exec/access_walk.c', 'exec/lease.c', 'exec/redir_access.c', 'exec/surface_query.c', 'drivers/video/cirrus/backend_cirrus.c', 'drivers/video/cirrus/cirrus_identify.c', 'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pegc.c', 'gfx/gfx_core.c', 'gfx/gfx_scroll.c', 'gfx/gfx_vram.c', 'kapi/kapi_db.c', 'kernel/console/console.c', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'kernel/sys.c', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/host32.py', 'tools/tests/kapi_bounds_host.c', 'tools/tests/mutpar.py', 'tools/tests/test_gfx_bounds.py', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/kernel_layout.py', 'tools/tests/test_kapi_bounds.py', 'tools/tests/gfx_bounds_cases.inc', 'tools/check_map.d/check-kapi-bounds-host.yaml', 'build/checks.d/check-kapi-bounds-host.mk', 'lib/serial_diag.h', 'tools/tests/serial_diag_host.h', 'kernel/console/console_backend.h', 'platform/pc98/console.c', 'include/console_attr.h', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=5.020416)


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
