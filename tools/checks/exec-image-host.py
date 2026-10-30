#!/usr/bin/env python3
CHECK = dict(name='exec-image-host', tier=1,
    covers=['exec/exec.c', 'exec/exec_kapi_init.inc', 'exec/exec_image.inc', 'tools/tests/test_exec_image.py', 'tools/tests/exec_image_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/check_map.d/check-exec-image-host.yaml', 'build/checks.d/check-exec-image-host.mk'],
    needs=[], budget=60.0)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_exec_image.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
