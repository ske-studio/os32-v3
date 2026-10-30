#!/usr/bin/env python3
CHECK = dict(name='key-inject-host', tier=1,
    covers=['tools/np21w_mcp/gui.py', 'tools/gui_gate.py', 'tools/tests/key_inject_host.cpp', 'tools/tests/test_key_inject.py', 'tools/check_map.d/check-key-inject-host.yaml', 'build/checks.d/check-key-inject-host.mk', 'userland/gshell/src/taskbar.rs', 'userland/gshell/src/startmenu.rs'],
    needs=[], budget=1.456904)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_key_inject.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
