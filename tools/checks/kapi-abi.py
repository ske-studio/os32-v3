#!/usr/bin/env python3
"""KAPI は末尾追加だけ: 基点の sdk/kapi.json の口の並びが、今の並びの先頭にそのまま在る (tools/check_kapi_abi.py)。"""
CHECK = dict(name='kapi-abi', tier=1, needs=[], budget=5,
    covers=['sdk/kapi.json', 'tools/check_kapi_abi.py'],
    basis=['[ABI]', 'KAPI_SPEC §3-1'],
    kind='contract',
)


def run(ctx) -> int:
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/check_kapi_abi.py'], cwd=ctx.root)
