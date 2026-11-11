#!/usr/bin/env python3
"""U1: compile the userland entry point and the shared C2 converter without libc."""
import argparse
from pathlib import Path
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'userland/lib/gfx/v2/color.c'


def observe(work, source):
    path = work / 'color.c'
    path.write_text(source)
    exe = work / 'user'
    flags = ['-I' + str(ROOT / p) for p in
             ('.', 'include', 'sdk/include/os32', 'userland/lib/gfx/v2')]
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        *flags, str(path), str(ROOT / 'gfx/v2/format.c'),
        str(ROOT / 'tools/tests/gfx2_user_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory(prefix='gfx2-user-') as td:
        work = Path(td)
        assert observe(work, source) == 0
        print('GREEN U1 color pack and KAPI declarations', flush=True)
        if args.mutate:
            for anchor, replacement in [
                ('distance < best_distance', 'distance <= best_distance'),
                ('dr * dr + dg * dg + db * db', 'dr * dr + dg * dg + db * db * 0'),
                ('i < 256', 'i < 255'),
                ('return gfx2_from_argb(fmt, argb, out);',
                 'return gfx2_from_argb(fmt, argb, out) ? OS32_ERR_IO : 0;'),
            ]:
                assert source.count(anchor) == 1
                rc = observe(work, source.replace(anchor, replacement))
                assert rc != 0, (anchor, rc)
                print(f'RED runtime rc={rc}: {anchor}', flush=True)
    print('PASS U1')


if __name__ == '__main__':
    main()

