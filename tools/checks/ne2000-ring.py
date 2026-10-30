#!/usr/bin/env python3
CHECK = dict(name='ne2000-ring', tier=4,
    covers=['drivers/net/ne2000/ne2000_ring.c', 'tools/tests/ne2000_ring_test.c'],
    needs=[], budget=1.107864)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['mkdir', '-p', *shlex.split(ctx.make_vars['BUILD_OUT'])]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['gcc', *shlex.split(ctx.make_vars['C_STD']), '-Wall', '-Wextra', '-DNE2K_HOST_TEST', '-Idrivers/net/ne2000', '-o', *shlex.split(ctx.make_vars['BUILD_OUT'] + '/ne2000_ring_test'), 'tools/tests/ne2000_ring_test.c', 'drivers/net/ne2000/ne2000_ring.c']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = [*shlex.split(ctx.make_vars['BUILD_OUT'] + '/ne2000_ring_test')]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
