#!/usr/bin/env python3
"""Direct freestanding G2 display contract test."""
CHECK = dict(name='gf-v2-display-host', tier=1, needs=[], budget=5,
    covers=['drivers/video/geforce/gf_v2_display.c', 'drivers/video/geforce/gf_v2.h',
            'tools/tests/gf_v2_display_host.c', 'tools/tests/test_gf_v2_display.py',
            'tools/tests/host32.py'])
from pathlib import Path
import argparse
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    product = work / 'gf_v2_display.c'
    product.write_text(source)
    exe = work / 'test'
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-I' + str(ROOT), '-I' + str(ROOT / 'include'),
        *['-I' + str(ROOT / p) for p in ('mm', 'arch/x86', 'platform/pc98', 'gfx', 'exec', 'lib', 'drivers', 'sdk/include')],
        '-I' + str(ROOT / 'sdk/include/os32'), '-I' + str(ROOT / 'drivers/video/geforce'),
        str(product), str(ROOT / 'tools/tests/gf_v2_display_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'drivers/video/geforce/gf_v2_display.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            mutations = [
                ('g->home.mode_id, &g->home.front', 'm->id, &g->home.front', 2),
                ('== c->hold[head].after) return OS32_ERR_BUSY;', '== c->hold[head].after) return 0;', 7),
                ('front.fmt == GF3_INDEX8 && g->pal_set', 'front.fmt == GF3_INDEX8 && 0 && g->pal_set', 5),
            ]
            for anchor, replacement, expected in mutations:
                assert source.count(anchor) == 1
                rc = observe(work, source.replace(anchor, replacement))
                assert rc == expected, rc
                print(f'RED {anchor}: runtime rc={rc}')
    print('PASS G2 display contract')


if __name__ == '__main__':
    main()


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
