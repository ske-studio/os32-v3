#!/usr/bin/env python3
"""S8 stretch reference coordinates and filtering."""
CHECK = dict(name='gfx2-stretch-host', tier=1, needs=[], budget=5,
    covers=['gfx/v2/sw_stretch.c', 'gfx/v2/gfx2_sw.h', 'gfx/v2/gfx2_int.h',
            'include/**', 'mm/pgalloc.h', 'tools/tests/gfx2_stretch_host.c',
            'tools/tests/test_gfx2_stretch.py', 'tools/tests/host32.py', 'lib/divdi3.c'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]

def observe(work, source):
    path = work / 'sw_stretch.c'
    path.write_text(source)
    exe = work / 'stretch'
    flags = ['-I' + str(ROOT / p) for p in ('.', 'include', 'sdk/include', 'sdk/include/os32', 'mm', 'gfx/v2')]
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O0',
        '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', *flags,
        str(path), str(ROOT / 'tools/tests/gfx2_stretch_host.c'), str(ROOT / 'lib/divdi3.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/sw_stretch.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            old = '(u32)(f - 32768u)'
            assert source.count(old) == 1
            rc = observe(work, source.replace(old, '(u32)f'))
            assert rc == 2, rc
            print('RED filter center offset: runtime rc=2')
    print('PASS S8 nearest, bilinear, clipping and sync')

if __name__ == '__main__':
    main()

def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
