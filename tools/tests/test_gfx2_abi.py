#!/usr/bin/env python3
"""Public value records stay ABI-stable and independently GNU89 usable."""
CHECK = dict(name='gfx2-abi-host', tier=1, needs=[], budget=3,
    covers=['sdk/include/os32/os32_gfx2.h', 'tools/tests/gfx2_abi_host.c',
            'tools/tests/test_gfx2_abi.py', 'tools/tests/host32.py'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, header):
    (work / 'os32_gfx2.h').write_text(header)
    exe = work / 'abi'
    host32.build(['gcc', '-m32', '-std=gnu89', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-I' + str(work), str(ROOT / 'tools/tests/gfx2_abi_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    header = (ROOT / 'sdk/include/os32/os32_gfx2.h').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, header) == 0
        if args.mutate:
            # Keep total size fixed, so a compile error cannot count as RED.
            anchor = 'unsigned long reserved[6];'
            assert header.count(anchor) == 1
            assert observe(work, header.replace(anchor,
                'unsigned long reserved[5]; unsigned long padding;')) == 9
            print('RED HeadInfo reserved length: runtime rc=9')
    print('PASS all public record sizes and field offsets (GNU89, ILP32)')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
