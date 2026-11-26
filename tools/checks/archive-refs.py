#!/usr/bin/env python3
"""試験の覚え・ソースの註・道具の Python がアーカイブの文書を指す件数を、基準 (tools/ops/archive_refs_baseline.json) と照らす。"""
CHECK = dict(name='archive-refs', tier=1, needs=[], budget=5,
    covers=['tools/tests/*_tdd.md', '**/*.c', '**/*.h', '**/*.inc', '**/*.asm', '**/*.S', 'tools/**/*.py',
            'docs/archive/**', 'tools/ops/check_archive_refs.py', 'tools/ops/archive_refs_baseline.json'],
    basis=['CONSTRAINTS [D4]'],
    kind='characterization',
)


def run(ctx) -> int:
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/ops/check_archive_refs.py'], cwd=ctx.root)
