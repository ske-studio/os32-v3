#!/usr/bin/env python3
"""Exercise the C5 VRAM allocator and its failure atomicity."""
CHECK = dict(name='gfx2-heap-host', tier=1, needs=[], budget=5,
    covers=['gfx/v2/vram_heap.c', 'gfx/v2/gfx2_int.h',
            'tools/tests/gfx2_heap_host.c', 'tools/tests/test_gfx2_heap.py',
            'tools/tests/host32.py'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    path = work / 'vram_heap.c'
    path.write_text(source)
    exe = work / 'heap'
    flags = ['-I' + str(ROOT / p) for p in ('.', 'include', 'sdk/include', 'sdk/include/os32', 'mm', 'gfx/v2')]
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        *flags, str(path), str(ROOT / 'tools/tests/gfx2_heap_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/vram_heap.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            for anchor, replacement, expected in [
                ('if (i + 1 < h->n && !h->blk[i + 1].used)', 'if (0 && i + 1 < h->n && !h->blk[i + 1].used)', 2),
                ('h->blk[i].off == off && h->blk[i].used', 'h->blk[i].off == off', 3),
                ('extra > GFX2_HEAP_BLOCKS - h->n', 'extra > GFX2_HEAP_BLOCKS', 7),
            ]:
                assert source.count(anchor) == 1
                rc = observe(work, source.replace(anchor, replacement))
                assert rc == expected, (anchor, rc)
                print(f'RED runtime rc={rc}: {anchor}')
    print('PASS C5 heap')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
