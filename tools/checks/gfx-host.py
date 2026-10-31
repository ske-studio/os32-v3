#!/usr/bin/env python3
CHECK = dict(name='gfx-host', tier=1,
    covers=['tools/tests/paging_source.py', 'include/gfx_format.h', 'exec/access_walk.c', 'exec/lease.c', 'exec/redir_access.c', 'exec/surface_query.c', 'drivers/video/cirrus/backend_cirrus.c', 'drivers/video/cirrus/cirrus_identify.c', 'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pegc.c', 'gfx/gfx_core.c', 'gfx/palette.c', 'gfx/gfx_scroll.c', 'gfx/gfx_vram.c', 'mm/paging.c', 'platform/pc98/platform_memory_desc.h', 'include/platform_memory.h', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'kernel/sys.c', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/kernel_layout.py', 'lib/serial_diag.h', 'tools/tests/serial_diag_host.h', 'gfx/gfx_internal.h', 'tools/tests/gfx_reinit_host.c', 'tools/tests/test_gfx_reinit.py', 'lib/utf8.c', 'tools/tests/gfx_attach_host.c', 'tools/tests/test_gfx_attach.py', 'userland/lib/gfx/**', 'tools/tests/test_gfx_reattach.py', 'tools/tests/gfx_reattach_host.c', 'tools/run_host_group.py', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=15.023988)


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
