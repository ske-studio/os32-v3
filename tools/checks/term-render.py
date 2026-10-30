#!/usr/bin/env python3
CHECK = dict(name='term-render', tier=1,
    covers=['userland/libos32term_render/**', 'userland/libos32term/**'],
    needs=[], budget=0.961692)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['cargo', 'test', '--manifest-path', 'userland/libos32term_render/Cargo.toml', '--target', 'x86_64-unknown-linux-gnu', '--offline']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['cargo', 'check', '--manifest-path', 'userland/libos32term_render/Cargo.toml', '--lib', '--target', 'x86_64-unknown-linux-gnu', '--offline']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
