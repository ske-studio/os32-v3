#!/usr/bin/env python3
CHECK = dict(name='docs-status', tier=0,
    covers=['docs/tasks/**/*.md', 'docs/POLICY_DEV.md', 'tools/check_docs_status.py', 'docs/tasks/DEFERRED_TESTS.md'],
    needs=[], budget=0.852328)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/check_docs_status.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
