#!/usr/bin/env python3
"""G4 fence ring against an isolated fake channel."""
from pathlib import Path
import argparse
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'drivers/video/geforce/gf_v2_fence.c'

def observe(work, source):
    path = work / 'fence.c'
    path.write_text(source)
    exe = work / 'fence'
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                  '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
                  '-I' + str(ROOT), '-I' + str(SOURCE.parent), *include_flags(ROOT),
                  str(path), str(ROOT / 'tools/tests/gf_v2_fence_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    anchor = 'if (r->count == GF_V2_RING) {'
    with tempfile.TemporaryDirectory(prefix='gfx2-fence-') as td:
        work = Path(td)
        assert observe(work, source) == 0
        print('GREEN ring full, completion and failure')
        if args.mutate:
            assert source.count(anchor) == 1
            assert observe(work, source.replace(anchor, 'if (r->count > GF_V2_RING) {')) > 0
            print('RED full ring guard')
            missing = '    return 0;\n}\n'
            source_wait = source.rsplit(missing, 1)
            assert len(source_wait) == 2
            assert observe(work, source_wait[0] + '    return OS32_ERR_INVAL;\n}\n' + source_wait[1]) > 0
            print('RED absent fence wait')
            assert observe(work, source) == 0
            print('GREEN restored source')

if __name__ == '__main__':
    main()
