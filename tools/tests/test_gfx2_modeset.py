#!/usr/bin/env python3
"""Exercise mode transition failure atomicity."""
CHECK = dict(name='gfx2-modeset-host', tier=1, needs=[], budget=5,
    covers=['gfx/v2/modeset.c', 'gfx/v2/gfx2_int.h',
            'tools/tests/gfx2_modeset_host.c', 'tools/tests/test_gfx2_modeset.py',
            'tools/tests/host32.py'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]

def observe(work, source):
    path = work / 'modeset.c'
    path.write_text(source.replace('#include "../gfx_policy.h"', '#include "gfx/gfx_policy.h"').replace('#include "../gfx.h"', '#include "gfx/gfx.h"'))
    exe = work / 'test'
    flags = ['-I' + str(ROOT / p) for p in ('.', 'include', 'sdk/include', 'sdk/include/os32', 'mm', 'gfx/v2')]
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', *flags, str(path), str(ROOT / 'tools/tests/gfx2_modeset_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/modeset.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            for anchor, replacement, expected in [
                ('if (rc) return rc;\n    if (a->state == GFX2_AD_LOST_HELD', 'if (0) return rc;\n    if (a->state == GFX2_AD_LOST_HELD', 1),
                ('if (rc) return rc;\n    if (a->state != GFX2_AD_OK)', 'if (0) return rc;\n    if (a->state != GFX2_AD_OK)', 2),
            ]:
                assert source.count(anchor) == 1
                rc = observe(work, source.replace(anchor, replacement))
                assert rc == expected, (anchor, rc)
                print(f'RED runtime rc={rc}: {anchor!r}')
    print('PASS C12 modeset')

if __name__ == '__main__':
    main()

def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
