#!/usr/bin/env python3
"""行番号つきの参照の検査の道具 (tools/check_doc_line_refs.py) の単体試験と変異。"""
CHECK = dict(name='docs-line-refs-host', tier=1, needs=[], budget=5,
    covers=['tools/check_doc_line_refs.py', 'tools/tests/test_doc_line_refs.py', 'tools/doc_line_refs_baseline.json'],
    basis=['POLICY_DEV §9'],
)


def run(ctx) -> int:
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_doc_line_refs.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
