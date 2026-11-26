#!/usr/bin/env python3
CHECK = dict(name='docs-links', tier=0,
    covers=['kernel/v86/*', 'platform/pc98/legacy/*', 'sys/dev/hostdrv/hostdrvfs_proto.h', 'sys/fs/ext2/ext2_dir.c', 'sys/fs/ext2/ext2_vfs.c', 'sys/dev/hostdrv/hostdrvfs.c', 'docs/**/*.md', 'CLAUDE.md', 'README.md', 'tools/tests/*_tdd.md', 'arch/README.md', 'platform/README.md', 'tools/ops/check_docs_links.py', 'tools/tests/test_docs_links.py', 'sys/kern/mm/paging_app.c', 'sys/kern/mm/paging_app.h', 'sys/platform/pc98/legacy/v86_io.c'],
    needs=[], budget=3.1,
    basis=['[D4]'],
)


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
    argv = ['python3', 'tools/ops/check_docs_links.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
