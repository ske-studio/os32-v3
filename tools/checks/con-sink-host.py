#!/usr/bin/env python3
CHECK = dict(name='con-sink-host', tier=1,
    covers=['tools/tests/con_sink_arch/arch_io.h', 'tools/tests/host_arch/arch_io.h', 'sys/platform/pc98/boot_serial.inc', 'sys/include/console.h', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/platform/pc98/video/pc98_route.h', 'tools/tests/host32.py', 'sys/kern/console/con_sink.c', 'sys/kern/console/console.c', 'tools/tests/con_sink_host.c', 'tools/tests/test_con_sink.py', 'sys/include/port_service.h', 'sys/include/console_output.h', 'sys/kern/port_service.c', 'sys/kern/console/console_backend.h', 'sys/platform/pc98/console.c', 'sys/include/console_attr.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3.5,
    basis=['KAPI_SPEC §console シンクと実 RAM 量 (v46)'],
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_con_sink.py']
    if ctx.mutate:
        argv.append('--mutate')
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
