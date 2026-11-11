#!/usr/bin/env python3
"""Exercise HAL v2 packed formats without libc."""
CHECK = dict(name='gfx2-format-host', tier=1, needs=[], budget=10,
    covers=['gfx/v2/format.c', 'gfx/v2/gfx2_sw.h', 'gfx/v2/gfx2_int.h',
            'tools/tests/gfx2_format_host.c', 'tools/tests/test_gfx2_format.py',
            'tools/tests/host32.py'])
from pathlib import Path
import argparse
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    path = work / 'format.c'
    path.write_text(source)
    exe = work / 'format'
    flags = ['-I' + str(ROOT / p) for p in ('.', 'include', 'sdk/include', 'sdk/include/os32', 'mm', 'gfx/v2')]
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        *flags, str(path), str(ROOT / 'tools/tests/gfx2_format_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/format.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            for anchor, replacement in [
                ('fmt < GFX2_FMT_INDEX8 || fmt > GFX2_FMT_UYVY || !o', 'fmt > GFX2_FMT_UYVY || !o'),
                ('(v << 3) | (v >> 2)', '(v << 3)'),
                ('298 * c + 409 * e + 128', '298 * c + 409 * e'),
            ]:
                assert source.count(anchor) == 1
                rc = observe(work, source.replace(anchor, replacement))
                assert rc != 0, (anchor, rc)
                print(f'RED runtime rc={rc}: {anchor}')
    print('PASS C2 format')


if __name__ == '__main__':
    main()


def run(ctx):
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)
