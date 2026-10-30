#!/usr/bin/env python3
CHECK = dict(name='docs-links', tier=0,
    covers=['fs/**', 'docs/**/*.md', 'CLAUDE.md', 'README.md', 'tools/tests/*_tdd.md', 'arch/README.md', 'platform/README.md', 'tools/check_docs_links.py', 'tools/tests/test_docs_links.py', 'mm/paging_app.c', 'mm/paging_app.h', 'kernel/v86/v86_io.c'],
    needs=[], budget=0.982672)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_docs_links.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', 'tools/check_docs_links.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
