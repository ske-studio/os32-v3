#!/usr/bin/env python3
"""Test calibrated sync gap conversion without libc."""
CHECK = dict(name='gfxsync-gap', tier=1, needs=[], budget=5,
    covers=['gfx/sync/sync_gap.c', 'gfx/sync/sync_int.h',
            'tools/tests/gfxsync_gap_host.c', 'tools/tests/test_gfxsync_gap.py',
            'tools/tests/host32.py'])
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]

def observe(work, source):
    src = work / 'sync_gap.c'
    src.write_text(source)
    exe = work / 'test'
    dirs = ['gfx/sync', 'include', 'sdk/include/os32']
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O2', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        *['-I' + str(ROOT / d) for d in dirs], str(src),
        str(ROOT / 'tools/tests/gfxsync_gap_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/sync/sync_gap.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        print('GREEN baseline')
        if args.mutate:
            anchor = '#define GFXSYNC_TICK_NS 10000000u'
            assert source.count(anchor) == 1
            assert observe(work, source.replace(anchor, '#define GFXSYNC_TICK_NS 1000000u')) == 1
            print('RED tick conversion')
            assert observe(work, source) == 0
            print('GREEN restored')

def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)

if __name__ == '__main__':
    main()
