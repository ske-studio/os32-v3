"""L3: geometry spans retain SHADOW pixels and queue DIRECT fills."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

ROOT = Path(__file__).resolve().parents[2]
FILL = 'userland/lib/gfx/geom/gfx_fill.c'
CIRCLE = 'userland/lib/gfx/geom/gfx_circle.c'
CHECK = dict(name='gfx-geom-direct', tier=1, needs=[], budget=15, covers=[
    'userland/lib/gfx/geom/gfx_fill.c', 'userland/lib/gfx/geom/gfx_circle.c',
    'userland/lib/gfx/draw/gfx_cmd.c',
    'userland/lib/gfx/gfx_direct_internal.h', 'userland/lib/gfx/libgfx_internal.h',
    'userland/lib/gfx/libos32gfx.h', 'tools/tests/gfx_geom_direct_host.c',
    'tools/tests/test_gfx_geom_direct.py', 'tools/tests/host32.py',
    'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
MUTATIONS = (
    ('fill-branch', FILL, 'if (gfx_direct) {', 'if (0) {'),
    ('circle-branch', CIRCLE, 'if (gfx_direct) {', 'if (0) {'),
    ('fill-width', FILL, 'x_end - x_start + 1, 1, color, 0', 'x_end - x_start, 1, color, 0'),
    ('circle-coordinate', CIRCLE, 'cx - x, cy + y, x * 2 + 1, 1, color, 0', 'cx - x + 1, cy + y, x * 2 + 1, 1, color, 0'),
)


def run_case(folder, runner, mutation=None):
    edits = {}
    if mutation:
        name, path, old, new = mutation
        source = (ROOT / path).read_text()
        assert source.count(old) == 1, name
        edits[path] = source.replace(old, new)
    exe = folder / 'geom-host'
    command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
               '-Werror', '-Wno-unused-function', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
               '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static', '-no-pie',
               '-Wl,--gc-sections', *include_flags(ROOT), '-I' + str(ROOT / 'userland/lib/gfx'),
               *(str(ROOT / p) for p in ('tools/tests/gfx_geom_direct_host.c', FILL, CIRCLE,
                                          'userland/lib/gfx/draw/gfx_cmd.c')), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    if mutation:
        assert result.returncode == 1 and result.stdout.startswith('FAIL '), result
    else:
        assert result.returncode == 0, result
    print('MUTATE RED ' + mutation[0] if mutation else 'PASS L3 geometry (' + runner + ')')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        for mutation in ((None, *MUTATIONS, None) if args.mutate else (None,)):
            with tempfile.TemporaryDirectory(prefix='gfx-geom-') as td:
                run_case(Path(td), runner, mutation)


def run(ctx) -> int:
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx_geom_direct.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
