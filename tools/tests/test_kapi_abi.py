#!/usr/bin/env python3
"""D0: runtime checks of the x86 generator's fixed argument ABI provider."""
CHECK = dict(name='kapi-abi-host', tier=1, needs=[], budget=5,
    covers=['sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/kapi.json', 'sdk/target_profile.py',
            'tools/tests/test_kapi_abi.py'])
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]

PROBE = '''import importlib.util, sys
sys.path.insert(0, 'sdk')
spec = importlib.util.spec_from_file_location('abi', sys.argv[1])
abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi)
assert abi.fixed_arg_bytes([]) == 0
assert abi.fixed_arg_bytes(['char a', 'i16 b', 'u32 c', 'const void *p', '...']) == 16
for args, expected in [(['int a'], 4), (['u8 a', 'u16 b'], 8),
                       (['int a', 'void *b', 'u32 c'], 12)]:
    assert abi.fixed_arg_bytes(args) == expected
try:
    abi.fixed_arg_bytes([], 'ppc-powermac')
except ValueError:
    pass
else:
    raise AssertionError('PPC generator ABI accepted')
import json
rows = json.load(open('sdk/kapi.json'))['api']
maximum = max(sum(a != '...' for a in row.get('args', [])) for row in rows)
for count in range(maximum + 1):
    assert abi.fixed_arg_bytes(['u32 a%d' % i for i in range(count)]) == 4 * count
for row in rows:
    args = row.get('args', [])
    assert abi.fixed_arg_bytes(args) == 4 * sum(a != '...' for a in args)
for kind in ['double', 'float', 'long long', 'struct aggregate', 'unknown_type']:
    try:
        abi.fixed_arg_bytes([kind + ' value'])
    except ValueError:
        pass
    else:
        raise AssertionError('unsupported type accepted: ' + kind)
'''


def inspect_provider(directory, source):
    path = directory / 'provider.py'
    path.write_text(source)
    return subprocess.run([sys.executable, '-B', '-c', PROBE, str(path)],
                          capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'sdk/kapi_abi_x86.py').read_text()
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp)
        result = inspect_provider(directory, source)
        assert result.returncode == 0, result.stderr
        if args.mutate:
            anchor = 'return 4 * count'
            assert source.count(anchor) == 1
            assert inspect_provider(directory, source.replace(anchor, 'return 8 * count')).returncode != 0
            print('RED x86 slot width: runtime metadata mismatch')
            anchor = 'if kind not in _SCALARS:'
            assert source.count(anchor) == 1
            assert inspect_provider(directory, source.replace(anchor, 'if False:')).returncode != 0
            print('RED unsupported by-value types: runtime rejection missing')
    print('PASS x86 fixed integer/pointer slots and unsupported types')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
