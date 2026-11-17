#!/usr/bin/env python3
CHECK = dict(name='docs-status', tier=0,
    covers=['docs/tasks/**/*.md', 'docs/POLICY_DEV.md', 'tools/docs_status_exceptions.json', 'tools/check_docs_status.py', 'docs/tasks/DEFERRED_TESTS.md'],
    needs=[], budget=2.7,
    basis=['POLICY_DEV §8'],
    full_only=True,
)


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
