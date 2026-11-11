#!/usr/bin/env python3
"""Exercise clipped HOSTDATA, source keys and synchronized writes."""
from pathlib import Path
import argparse
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    path = work / 'sw_hostdata.c'
    path.write_text(source)
    exe = work / 'hostdata'
    flags = ['-I' + str(ROOT / p) for p in ('.', 'include', 'sdk/include', 'sdk/include/os32', 'mm', 'gfx/v2')]
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', *flags, str(path), str(ROOT / 'tools/tests/gfx2_hostdata_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/sw_hostdata.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            anchor = 'pixel >= op->skey.v && pixel <= op->skey.x'
            assert source.count(anchor) == 1
            rc = observe(work, source.replace(anchor, '0'))
            assert rc == 1, rc
            print(f'RED runtime rc={rc}: source key')
    print('PASS S6 hostdata')


if __name__ == '__main__':
    main()
