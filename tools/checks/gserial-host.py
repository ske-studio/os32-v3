#!/usr/bin/env python3
CHECK = dict(name='gserial-host', tier=1,
    covers=['tools/lanfs_host.py', 'tools/lan_bridge.py', 'tools/gserial.py', 'tools/rshell_serial.py', 'tools/rshell_peek.py', 'tools/serialfs_host.py', 'tools/tests/test_gserial_host.py'],
    needs=[], budget=0.794472)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_gserial_host.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
