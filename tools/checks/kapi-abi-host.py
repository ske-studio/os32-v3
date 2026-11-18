#!/usr/bin/env python3
"""KAPI の末尾追加の検査の道具 (tools/check_kapi_abi.py) の単体試験と変異。"""
CHECK = dict(name='kapi-abi-host', tier=1, needs=[], budget=10,
    covers=['sdk/kapi.json', 'tools/check_kapi_abi.py', 'tools/tests/test_kapi_abi.py'],
    basis=['[ABI]', 'KAPI_SPEC §3-1'],
)


def run(ctx) -> int:
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_kapi_abi.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
