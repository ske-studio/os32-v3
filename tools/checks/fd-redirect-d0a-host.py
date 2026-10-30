#!/usr/bin/env python3
CHECK = dict(name='fd-redirect-d0a-host', tier=1,
    covers=['tools/tests/mutpar.py', 'exec/redir_access.c', 'tools/tests/test_fd_redirect_d0a.py', 'tools/tests/fd_redirect_d0a_host.c', 'fs/fd_redirect.c', 'tools/tests/kernel_includes.py'],
    needs=[], budget=1.340944)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_fd_redirect_d0a.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
