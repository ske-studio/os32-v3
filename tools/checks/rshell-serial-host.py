#!/usr/bin/env python3
CHECK = dict(name='rshell-serial-host', tier=1,
    covers=['tools/lanfs_host.py', 'tools/lan_bridge.py', 'tools/rshell_serial.py', 'tools/rshell_peek.py', 'tools/serialfs_host.py', 'tools/tests/test_rshell_serial.py', 'tools/check_map.d/check-rshell-serial-host.yaml', 'build/checks.d/check-rshell-serial-host.mk'],
    needs=[], budget=0.995684)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_rshell_serial.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
