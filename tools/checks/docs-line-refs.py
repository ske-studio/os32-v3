#!/usr/bin/env python3
"""文書が、このリポジトリのソースを行番号つきで指す件数を基準 (tools/doc_line_refs_baseline.json) と照らす。"""
CHECK = dict(name='docs-line-refs', tier=1, needs=[], budget=5,
    covers=['**/*.md', 'tools/check_doc_line_refs.py', 'tools/doc_line_refs_baseline.json'],
    basis=['POLICY_DEV §9'],
    kind='characterization',
)


def run(ctx) -> int:
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/check_doc_line_refs.py'], cwd=ctx.root)
