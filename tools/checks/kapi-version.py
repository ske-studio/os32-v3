#!/usr/bin/env python3
CHECK = dict(name='kapi-version', tier=0,
    covers=['docs/INDEX.md', 'docs/KAPI_SPEC.md', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/kapi_generated.rs', 'tools/check_kapi_version.py', 'userland/rust/libos32gui/src/cfgro.rs', 'README.md', 'sdk/kapi.json'],
    needs=[], budget=0.738152)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/check_kapi_version.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
