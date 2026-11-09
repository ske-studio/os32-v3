"""Unmapped framebuffers: raw helpers, planar tilemap and Markdown scrolling."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

ROOT = Path(__file__).resolve().parents[2]
MD = 'userland/lib/md/md_render.c'
TILE = 'userland/lib/tilemap/tilemap_compose.c'
CHECK = dict(name='gfx-plane-guard-host', tier=1, needs=[], budget=15, covers=[
    'userland/lib/md/md_render.c', 'userland/lib/tilemap/tilemap_compose.c', 'userland/lib/md/*.h', 'userland/lib/tilemap/*.h',
    'userland/lib/tilemap/asm_tilemap.asm', 'userland/lib/asset/libos32asset.h',
    'userland/lib/gfx/libgfx_internal.h',
    'userland/lib/gfx/libos32gfx.h', 'userland/lib/gfx/gfx_direct_internal.h',
    'sdk/include/os32/**', 'tools/tests/gfx_plane_guard_host.c',
    'tools/tests/test_gfx_plane_guard.py', 'tools/tests/host32.py',
    'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
MUTATIONS = (
    ('md-direct-guard', MD, '!gfx_ready || gfx_direct || !gfx_fb.planes[0]', '!gfx_ready || !gfx_fb.planes[0]'),
    *[(name+'-null', TILE,
       'void tilemap_compose_'+name+'(void)\n{\n    if (!gfx_ready) return;\n    if (!gfx_fb.planes[0]) return;',
       'void tilemap_compose_'+name+'(void)\n{\n    if (!gfx_ready) return;')
      for name in ('btf','ftb','btf_fast','scroll')],
)


def run_case(folder, runner, mutation=None):
    edits = {}
    if mutation:
        name, path, old, new = mutation
        source = (ROOT / path).read_text()
        assert source.count(old) == 1, name
        edits[path] = source.replace(old, new)
    exe = folder / 'hal-host'
    obj = folder / 'tile.o'
    subprocess.run(['nasm', '-f', 'elf32', str(ROOT/'userland/lib/tilemap/asm_tilemap.asm'), '-o', str(obj)], check=True)
    command = [str(Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))/'bin/i386-elf-gcc'),
               '-std=gnu11', '-O2', '-ffreestanding', '-fno-builtin',
               '-ffunction-sections', '-fdata-sections', '-nostdlib', '-Wl,--gc-sections',
               *include_flags(ROOT),
               *['-I'+str(ROOT/'userland/lib'/name) for name in ('gfx','md','tilemap','asset')],
               str(ROOT/'tools/tests/gfx_plane_guard_host.c'), str(ROOT/MD), str(ROOT/TILE), str(obj), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), result
    if mutation:
        assert result.stdout.startswith('FAIL '), result
    print(('MUTATE RED ' + mutation[0]) if mutation else 'PASS plane guards (' + runner + ')')


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
    argv = ['python3', '-B', 'tools/tests/test_gfx_plane_guard.py', *(['--mutate'] if ctx.mutate else [])]
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
