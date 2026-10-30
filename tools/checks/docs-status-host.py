#!/usr/bin/env python3
CHECK = dict(name='docs-status-host', tier=0,
    covers=['docs/tasks/**/*.md', 'docs/POLICY_DEV.md', 'tools/check_docs_status.py', 'tools/tests/test_docs_status.py', 'tools/check_map.d/check-docs-status-host.yaml', 'build/checks.d/check-docs-status-host.mk', 'docs/tasks/DEFERRED_TESTS.md'],
    needs=[], budget=0.831226)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_docs_status.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
