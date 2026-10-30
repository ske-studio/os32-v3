#!/usr/bin/env python3
CHECK = dict(name='docs-tree-host', tier=1,
    covers=['tools/doc_graph.py', 'tools/tests/test_doc_graph.py', 'tools/doc_delink_archive.py', 'tools/tests/test_doc_delink_archive.py', '**/*.md', 'tools/doc_tree_baseline.json'],
    needs=[], budget=1.658448)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_doc_graph.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_doc_delink_archive.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
