#!/usr/bin/env python3
"""S5 monochrome expansion and transparent clipping."""
CHECK = dict(name='gfx2-mono-host', tier=1, needs=[], budget=5,
    covers=['gfx/v2/sw_mono.c', 'gfx/v2/gfx2_sw.h', 'gfx/v2/gfx2_int.h',
            'include/gfx_sync.h', 'tools/tests/gfx2_mono_host.c',
            'tools/tests/test_gfx2_mono.py', 'tools/tests/host32.py'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]

def observe(work, source):
    path = work / 'sw_mono.c'
    path.write_text(source)
    exe = work / 'mono'
    flags = ['-I' + str(ROOT / p) for p in ('.', 'include', 'sdk/include', 'sdk/include/os32', 'mm', 'gfx/v2')]
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        *flags, str(path), str(ROOT / 'tools/tests/gfx2_mono_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/sw_mono.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            old, new = '0x80u >> (bit & 7)', '1u << (bit & 7)'
            assert source.count(old) == 1
            rc = observe(work, source.replace(old, new))
            assert rc != 0, rc
            print(f'RED runtime rc={rc}: MSB first')
    print('PASS S5 mono')

def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)

if __name__ == '__main__':
    main()
