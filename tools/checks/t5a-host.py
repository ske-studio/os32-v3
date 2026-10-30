#!/usr/bin/env python3
CHECK = dict(name='t5a-host', tier=1,
    covers=['userland/rust/t5a_display/**', 'userland/libos32term/Cargo.toml', 'userland/libos32term/src/clip.rs', 'userland/libos32term/src/lib.rs', 'userland/libos32term/src/model.rs', 'userland/libos32term/src/stream.rs', 'userland/libos32term/src/utf8.rs', 'userland/libos32term/tests/clip.rs', 'userland/libos32term/tests/model.rs', 'userland/libos32term/tests/stream.rs', 'userland/libos32term/tests/utf8.rs', 'userland/libos32term_render/Cargo.toml', 'userland/libos32term_render/src/lib.rs', 'userland/libos32term_render/src/rework_tests.rs', 'userland/libos32term_render/tests/render.rs', 'userland/libos32term_render/tests/rework.rs'],
    needs=[], budget=1.01649)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['cargo', 'test', '--manifest-path', 'userland/rust/t5a_display/host_tests/Cargo.toml', '--target', 'x86_64-unknown-linux-gnu', '--offline']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
