#!/usr/bin/env python3
"""Test GeForce v2 lifecycle with a freestanding fake channel."""
CHECK = dict(name='gf-v2-life-host', tier=1, needs=[], budget=5,
    covers=['drivers/video/geforce/gf_v2_life.c', 'drivers/video/geforce/gf_v2_fence.c',
            'drivers/video/geforce/gf_v2.h', 'tools/tests/gf_v2_life_host.c',
            'tools/tests/test_gf_v2_life.py', 'tools/tests/host32.py'])
from pathlib import Path
import argparse
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    src = work / 'life.c'
    src.write_text(source)
    exe = work / 'test'
    includes = ['.', 'include', 'sdk/include/os32', 'arch/x86', 'mm', 'drivers/video/geforce']
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        *['-I' + str(ROOT / p) for p in includes], str(src),
        str(ROOT / 'drivers/video/geforce/gf_v2_fence.c'),
        str(ROOT / 'tools/tests/gf_v2_life_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'drivers/video/geforce/gf_v2_life.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            for anchor, replacement in [
                ('if (!rc) return 0;', 'if (!rc) rc = GF2_ETIME;'),
                ('if (g->epoch != c->ch->epoch) gf_v2_fence_reset(g);', '(void)g->epoch;'),
            ]:
                assert source.count(anchor) == 1
                rc = observe(work, source.replace(anchor, replacement))
                assert rc != 0, (anchor, rc)
                print(f'RED {anchor}: runtime rc={rc}')
    print('PASS GF v2 lifecycle')


if __name__ == '__main__':
    main()


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
