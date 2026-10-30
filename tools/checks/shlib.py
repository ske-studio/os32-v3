#!/usr/bin/env python3
CHECK = dict(name='shlib', tier=1,
    covers=['sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/mkshlib.py', 'userland/rust/libos32gui/src/shlib.rs'],
    needs=[], budget=0.64464)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/mkshlib.py', '--check']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
