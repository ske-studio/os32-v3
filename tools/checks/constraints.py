#!/usr/bin/env python3
CHECK = dict(name='constraints', tier=0,
    covers=['CLAUDE.md', 'docs/CONSTRAINTS.md', 'tools/check_constraints.py'],
    needs=[], budget=0.674118)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/check_constraints.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
