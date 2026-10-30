#!/usr/bin/env python3
CHECK = dict(name='privileged', tier=1,
    covers=['userland/**', 'tools/check_privileged.py', 'tools/check_privileged_allow.txt', 'tools/tests/test_check_privileged.py', 'tools/check_map.d/check-privileged.yaml', 'build/checks.d/check-privileged.mk'],
    needs=[], budget=7.120772)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_check_privileged.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', 'tools/check_privileged.py', '--strict']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
