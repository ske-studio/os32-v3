#!/usr/bin/env python3
CHECK = dict(name='con-sink-host', tier=1,
    covers=['drivers/video/pc98/pc98_route.h', 'tools/tests/host32.py', 'kernel/console/con_sink.c', 'kernel/console/console.c', 'tools/tests/con_sink_host.c', 'tools/tests/test_con_sink.py', 'tools/check_map.d/check-con-sink-host.yaml', 'build/checks.d/check-con-sink-host.mk', 'include/port_service.h', 'include/console_output.h', 'kernel/port_service.c', 'kernel/console/console_backend.h', 'platform/pc98/console.c', 'include/console_attr.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=1.629642)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_con_sink.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
