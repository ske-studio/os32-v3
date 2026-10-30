#!/usr/bin/env python3
CHECK = dict(name='gui-proto', tier=1,
    covers=['sdk/rust/os32api/src/gui/proto.rs', 'tools/check_gui_proto.py'],
    needs=[], budget=0.72346)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/check_gui_proto.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
