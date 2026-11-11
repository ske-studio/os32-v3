#!/usr/bin/env python3
"""Direct freestanding G10 enumeration contract test."""
CHECK = dict(name='gf-v2-enum-host', tier=1, needs=[], budget=5,
    covers=['drivers/video/geforce/gf_v2_enum.c', 'drivers/video/geforce/gf_v2.h',
            'tools/tests/gf_v2_enum_host.c', 'tools/tests/test_gf_v2_enum.py',
            'tools/tests/host32.py'])
from pathlib import Path
import argparse
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    product = work / 'gf_v2_enum.c'
    product.write_text(source)
    exe = work / 'test'
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-I' + str(ROOT), '-I' + str(ROOT / 'include'),
        *['-I' + str(ROOT / p) for p in ('mm', 'arch/x86', 'platform/pc98', 'gfx', 'exec', 'lib', 'drivers', 'sdk/include')],
        '-I' + str(ROOT / 'sdk/include/os32'), '-I' + str(ROOT / 'drivers/video/geforce'),
        str(product), str(ROOT / 'tools/tests/gf_v2_enum_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'drivers/video/geforce/gf_v2_enum.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            anchor = '(c->routes[j].head_mask & (1u << head))'
            assert source.count(anchor) == 1
            rc = observe(work, source.replace(anchor, '(1u << head)'))
            assert rc == 6, rc
            print('RED head mask: runtime rc=6')
            anchor = 'pair = paired(c, r);'
            assert source.count(anchor) == 1
            rc = observe(work, source.replace(anchor, 'pair = 0; (void)paired;'))
            assert rc == 2, rc
            print('RED DVI-I pairing: runtime rc=2')
    print('PASS G10 enumeration contract')


if __name__ == '__main__':
    main()


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
