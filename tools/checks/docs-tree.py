#!/usr/bin/env python3
CHECK = dict(name='docs-tree', tier=1,
    covers=['**/*.md', 'tools/doc_graph.py', 'tools/doc_tree_baseline.json'],
    needs=[], budget=5.103676)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/doc_graph.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
