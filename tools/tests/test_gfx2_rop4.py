#!/usr/bin/env python3
"""Exercise ROP4 mask selection, clipping, pattern and sync rows."""
import argparse
from pathlib import Path
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    path = work / 'sw_rop4.c'
    path.write_text(source)
    exe = work / 'rop4'
    flags = ['-I' + str(ROOT / p) for p in ('.', 'include', 'sdk/include', 'sdk/include/os32', 'mm', 'gfx/v2')]
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                  '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
                  *flags, str(path), str(ROOT / 'tools/tests/gfx2_rop4_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/sw_rop4.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            anchor = '(u8)op->rop : (u8)(op->rop >> 8)'
            assert source.count(anchor) == 1
            rc = observe(work, source.replace(anchor, '(u8)(op->rop >> 8) : (u8)op->rop'))
            assert rc == 2, rc
            print(f'RED runtime rc={rc}: mask selection')
    print('PASS S4 rop4')


if __name__ == '__main__':
    main()
