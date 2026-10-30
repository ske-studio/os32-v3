#!/usr/bin/env python3
CHECK = dict(name='gui-gate-host', tier=1,
    covers=['tools/gui_gate.py', 'tools/tests/test_gui_gate.py', 'tools/np21w_mcp/gui.py', 'userland/gshell/src/startmenu.rs', 'userland/gshell/src/taskbar.rs', 'tools/np21w_mcp/np21w_client.py'],
    needs=[], budget=1.08189)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_gui_gate.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
