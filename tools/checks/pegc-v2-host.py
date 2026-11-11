#!/usr/bin/env python3
CHECK = dict(name='pegc-v2-host', tier=1, needs=[], budget=12,
    covers=['drivers/video/pc98/pegc_v2.c', 'gfx/v2/gfx2_int.h', 'include/gfx_sync.h', 'include/gfx_hal2.h', 'include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'gfx/palette.h', 'include/gfx_format.h', 'tools/tests/host32.py', 'drivers/video/pc98/backend_pegc.c', 'drivers/char/np2sysp.c', 'drivers/input/mouse_seamless.c', 'drivers/input/mouse.h', 'kernel/console/console.c', 'tools/tests/mutpar.py', 'tools/tests/pegc_mode_host.c', 'lib/serial_diag.h', 'tools/tests/serial_diag_host.h', 'include/port_service.h', 'include/console_output.h', 'kernel/port_service.c', 'kernel/console/console_backend.h', 'platform/pc98/console.c', 'include/console_attr.h', 'tools/tests/kernel_includes.py', 'tools/tests/test_pegc_v2.py', 'tools/tests/test_pegc_mode.py', 'drivers/video/pc98/backend_pegc_*.inc'])


def run(ctx):
    import os
    import subprocess
    if not ctx.runners:
        return 1
    for i, runner in enumerate(ctx.runners):
        argv = ['python3', '-B', 'tools/tests/test_pegc_v2.py']
        if ctx.mutate and i == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root,
                             env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0
