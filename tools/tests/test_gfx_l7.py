"""L7 present ordering for the filer and tilemap."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

CHECK = dict(name='gfx-l7-host', tier=1, needs=[], budget=15, covers=[
    'userland/lib/filer/filer_core.c', 'userland/lib/tilemap/tilemap_compose.c',
    'userland/lib/tilemap/tilemap_internal.h', 'userland/lib/filer/libos32filer.h',
    'userland/lib/tilemap/libos32tilemap.h', 'userland/lib/gfx/**',
    'tools/tests/gfx_l7_host.c', 'tools/tests/test_gfx_l7.py',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
ROOT = Path(__file__).resolve().parents[2]
FILER = 'userland/lib/filer/filer_core.c'
TILE = 'userland/lib/tilemap/tilemap_compose.c'
CMD = 'userland/lib/gfx/draw/gfx_cmd.c'
MUTATIONS = [
    ('filer-flush', FILER, 'if (gfx_direct) (void)gfx_flush();', 'if (0) (void)gfx_flush();'),
    ('tile-flush', TILE, 'if (gfx_direct) (void)gfx_flush();', 'if (0) (void)gfx_flush();'),
    ('command-value', CMD, '.x = x, .y = y, .w = w, .h = h, .color = color', '.x = x + 1, .y = y, .w = w, .h = h, .color = color'),
    ('shadow-condition', TILE, 'if (gfx_direct) (void)gfx_flush();', '(void)gfx_flush();'),
]

def run_case(folder, runner, mutation=None):
    edits = {}
    if mutation:
        name, path, old, new = mutation
        source = (ROOT / path).read_text()
        assert source.count(old) == 1, name
        edits[path] = source.replace(old, new)
    exe = folder / 'l7-host'
    cmd = [str(Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'), '-std=gnu11', '-O2', '-ffreestanding',
           '-fno-builtin', '-ffunction-sections', '-fdata-sections', '-nostdlib',
           '-Wl,--gc-sections', '-Wl,-e,_start', *include_flags(ROOT),
           '-I' + str(ROOT / 'userland/lib/gfx'), '-I' + str(ROOT / 'userland/lib/filer'),
           '-I' + str(ROOT / 'userland/lib/tilemap'),
           '-I' + str(ROOT / 'userland/lib/asset'),
           *(str(ROOT / p) for p in ('tools/tests/gfx_l7_host.c', FILER, TILE, CMD)), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [cmd])
    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), (mutation, result)
    print('RED ' + mutation[0] if mutation else 'PASS L7')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        for mutation in ((None, *MUTATIONS) if args.mutate else (None,)):
            with tempfile.TemporaryDirectory(prefix='gfx-l7-') as td:
                run_case(Path(td), runner, mutation)

def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx_l7.py', *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)

if __name__ == '__main__':
    main()
