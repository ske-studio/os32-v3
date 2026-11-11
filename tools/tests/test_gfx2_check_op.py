#!/usr/bin/env python3
"""Exercise capability selection with real and hypothetical surfaces."""
from pathlib import Path
import argparse
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    path = work / 'check_op.c'
    path.write_text(source)
    exe = work / 'probe'
    flags = ['-I' + str(ROOT / p) for p in ('.', 'include', 'sdk/include', 'sdk/include/os32', 'mm', 'gfx/v2')]
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O0',
                  '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
                  *flags, str(path), str(ROOT / 'tools/tests/gfx2_check_op_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/check_op.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            anchor = 'a->state == GFX2_AD_OK && !a->be->check(a->ctx, ex)'
            assert source.count(anchor) == 1
            rc = observe(work, source.replace(anchor, '!a->be->check(a->ctx, ex)'))
            assert rc == 4, rc
            print('RED runtime rc=4: lost adapter bypassed')
    print('PASS C8 check')


if __name__ == '__main__':
    main()
