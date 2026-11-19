import subprocess
import sys

CHECK = dict(name='ramfs-sqlite-host', tier=1, needs=[], budget=12,
             covers=['fs/ramfs/**', 'fs/vfs*', 'lib/kstring*', 'lib/sqlite3/**',
                     'tools/tests/ramfs_sqlite_host.c', 'tools/tests/test_ramfs_sqlite.py',
                     'tools/tests/sqlite_cc.py', 'tools/tests/cc_cache.py',
                     'tools/tests/kernel_includes.py'],
             basis=['PPC_SHELL_DESIGN §8'])


def run(ctx):
    args = [sys.executable, "-B", "tools/tests/test_ramfs_sqlite.py"]
    if ctx.mutate:
        args.append("--mutate")
    return subprocess.call(args, cwd=ctx.root)
