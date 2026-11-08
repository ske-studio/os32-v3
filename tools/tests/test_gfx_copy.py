"""L6: screen copy dispatch and overlap."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

CHECK = dict(name='gfx-copy-host', tier=1, needs=[], budget=15, covers=[
    'userland/lib/gfx/draw/gfx_copy.c', 'userland/lib/gfx/draw/gfx_cmd.c',
    'userland/lib/gfx/gfx_direct_internal.h', 'userland/lib/gfx/libgfx_internal.h',
    'userland/lib/gfx/libos32gfx.h', 'sdk/include/os32/**',
    'tools/tests/gfx_copy_host.c', 'tools/tests/test_gfx_copy.py',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'userland/lib/gfx/draw/gfx_copy.c'
MUTATIONS = tuple((name, SOURCE, old, new) for name, old, new in (
    ('dispatch', 'if (gfx_direct) {', 'if (1) {'),
    ('direction', 'ystep = dy > sy ? -1 : 1;', 'ystep = 1;'),
    ('packed-copy', '(unsigned int)(right - left));', '(unsigned int)0);'),
    ('copy-x', 'gfx_cmd_enqueue_copy(dx, dy, sx, sy, w, h)', 'gfx_cmd_enqueue_copy(dx + 1, dy, sx, sy, w, h)'),
))

def run_case(folder, runner, mutation=None):
    edits = {}
    if mutation:
        name, path, old, new = mutation
        source = (ROOT / path).read_text()
        assert source.count(old) == 1, name
        edits[path] = source.replace(old, new)
    exe = folder / 'hal-host'
    command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
               '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
               '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
               *include_flags(ROOT),
               str(ROOT / 'tools/tests/gfx_copy_host.c'),
               str(ROOT / 'userland/lib/gfx/draw/gfx_copy.c'),
               str(ROOT / 'userland/lib/gfx/draw/gfx_cmd.c'), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), result
    if mutation:
        assert result.stdout.startswith('FAIL '), result
    print(('MUTATE RED ' + mutation[0]) if mutation else 'PASS L6 screen copy (' + runner + ')')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        for mutation in (None, *MUTATIONS) if args.mutate else (None,):
            with tempfile.TemporaryDirectory(prefix='gfx-accel-hal-') as td:
                run_case(Path(td), runner, mutation)

        if args.mutate:
            with tempfile.TemporaryDirectory(prefix='gfx-cmd-restored-') as td:
                run_case(Path(td), runner)


def run(ctx) -> int:
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_gfx_copy.py', *(['--mutate'] if ctx.mutate else [])]
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
