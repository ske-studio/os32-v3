#!/usr/bin/env python3
"""G7 command stream against a freestanding fake channel."""
from pathlib import Path
import argparse
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'hwdev/gf/accel_rop3.c'

def observe(work, source):
    path = work / 'accel_rop3.c'
    path.write_text(source)
    exe = work / 'test'
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-I' + str(ROOT), '-I' + str(SOURCE.parent), *include_flags(ROOT),
        str(path), str(ROOT / 'tools/tests/gf_v2_rop3_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    anchor = 'value = kind == GF3_PAT_COLOR ? 2 : 1;'
    with tempfile.TemporaryDirectory(prefix='gf-rop3-') as td:
        work = Path(td)
        assert observe(work, source) == 0
        print('GREEN command streams, validation and failures')
        if args.mutate:
            assert source.count(anchor) == 1
            assert observe(work, source.replace(anchor, 'value = 1;')) > 0
            print('RED pattern selection')
            assert observe(work, source) == 0
            print('GREEN restored source')

if __name__ == '__main__':
    main()
