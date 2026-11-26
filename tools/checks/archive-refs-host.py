#!/usr/bin/env python3
"""アーカイブを指す参照の検査の道具 (tools/ops/check_archive_refs.py) の単体試験と変異。"""
CHECK = dict(name='archive-refs-host', tier=1, needs=[], budget=5,
    covers=['tools/ops/check_archive_refs.py', 'tools/tests/test_archive_refs.py', 'tools/ops/archive_refs_baseline.json',
            'docs/archive/README.md'],
    basis=['CONSTRAINTS [D4]'],
)


def run(ctx) -> int:
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_archive_refs.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
