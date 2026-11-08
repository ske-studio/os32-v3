"""L5: rectangle PUTs, affine row PUTs and synchronous backing-store GET."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

ROOT = Path(__file__).resolve().parents[2]
DRAW = 'userland/lib/gfx/draw/'
SOURCES = [DRAW + name + '.c' for name in
           ('gfx_blt', 'gfx_surface', 'gfx_sprite', 'gfx_rotate', 'gfx_cmd', 'gfx_draw')]
ASM = ['userland/lib/gfx/asm/' + name + '.asm' for name in
       ('asm_draw', 'asm_blit', 'asm_sprite')]
CHECK = dict(name='gfx-transfer-host', tier=1, needs=[], budget=20, covers=[
    'userland/lib/gfx/draw/gfx_blt.c',
    'userland/lib/gfx/draw/gfx_surface.c',
    'userland/lib/gfx/draw/gfx_sprite.c',
    'userland/lib/gfx/draw/gfx_rotate.c',
    'userland/lib/gfx/draw/gfx_cmd.c',
    'userland/lib/gfx/draw/gfx_draw.c',
    'userland/lib/gfx/asm/asm_draw.asm',
    'userland/lib/gfx/asm/asm_blit.asm',
    'userland/lib/gfx/asm/asm_sprite.asm', 'userland/lib/gfx/asm/gfx_const.inc',
    'userland/lib/gfx/libos32gfx.h', 'userland/lib/gfx/libgfx_internal.h',
    'userland/lib/gfx/gfx_direct_internal.h', 'userland/lib/math/trig.c',
    'userland/lib/math/libos32math.h', 'sdk/include/os32/os32api.h',
    'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_kapi_generated.h',
    'tools/tests/gfx_transfer_host.c', 'tools/tests/test_gfx_transfer.py',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
MUTATIONS = (
    ('surface-dispatch', DRAW+'gfx_surface.c',
     'if (gfx_direct) {\n        gfx_blit_direct(dx, dy, src, sx, sy, sw, sh, -1);',
     'if (1) {\n        gfx_blit_direct(dx, dy, src, sx, sy, sw, sh, -1);'),
    ('surface-width', DRAW+'gfx_surface.c',
     'gfx_cmd_enqueue_put(dx, dy + row, sw, rows, pixels, sw, key)',
     'gfx_cmd_enqueue_put(dx, dy + row, sw - 1, rows, pixels, sw, key)'),
    ('sprite-key', DRAW+'gfx_sprite.c', 'pixels, w, 255)', 'pixels, w, -1)'),
    ('surface-intersection', DRAW+'gfx_surface.c',
     'if (sw > gfx_fb.width - dx) sw = gfx_fb.width - dx;', ''),
    ('sprite-intersection', DRAW+'gfx_sprite.c',
     'if (w > gfx_fb.width - x) w = gfx_fb.width - x;', ''),
    ('surface-lost', DRAW+'gfx_surface.c', 'gfx_lost = 1;', 'gfx_lost = 0;'),
    ('sprite-lost', DRAW+'gfx_sprite.c', 'gfx_lost = 1;', 'gfx_lost = 0;'),
    ('affine-width', DRAW+'gfx_rotate.c',
     'gfx_cmd_enqueue_put(draw_x0, bbox_y + iy, width, 1,',
     'gfx_cmd_enqueue_put(draw_x0, bbox_y + iy, width - 1, 1,'),
    ('save-get', DRAW+'gfx_blt.c',
     'if (!gfx_cmd_get(sx * 8, y, width, h, pixels, width))', 'if (0)'),
)


def run_case(folder, runner, mutation=None):
    edits = {}
    if mutation:
        name, path, old, new = mutation
        source = (ROOT/path).read_text()
        assert source.count(old) == 1, (name, source.count(old))
        edits[path] = source.replace(old, new)
    objects = [folder/(Path(path).stem+'.o') for path in ASM]
    commands = [['nasm', '-f', 'elf32', '-I'+str(ROOT/'userland/lib/gfx/asm')+'/',
                 str(ROOT/path), '-o', str(obj)] for path, obj in zip(ASM, objects)]
    for command in commands:
        subprocess.run(command, check=True)
    commands = []
    exe = folder/'transfer-host'
    commands.append(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
                     '-Wextra', '-Werror', '-Wno-unused-variable', '-ffreestanding',
                     '-ffunction-sections', '-fdata-sections', '-fno-pie',
                     '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                     '-Wl,--gc-sections', *include_flags(ROOT),
                     '-I'+str(ROOT/'userland/lib/gfx'), '-I'+str(ROOT/'userland/lib/math'),
                     str(ROOT/'tools/tests/gfx_transfer_host.c'),
                     *[str(ROOT/path) for path in SOURCES],
                     str(ROOT/'userland/lib/math/trig.c'), *map(str, objects), '-o', str(exe)])
    build_in_tree(ROOT, folder, edits, commands)
    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    # A mutant must reach an assertion, never crash or merely fail to compile.
    assert result.returncode == (1 if mutation else 0), result
    if mutation:
        assert result.stdout.startswith('FAIL '), result
    print('MUTATE RED '+mutation[0] if mutation else 'PASS L5 transfers ('+runner+')')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        for mutation in (None, *MUTATIONS, None) if args.mutate else (None,):
            with tempfile.TemporaryDirectory(prefix='gfx-transfer-') as td:
                run_case(Path(td), runner, mutation)


def run(ctx) -> int:
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx_transfer.py',
                            *(['--mutate'] if ctx.mutate else [])],
                           cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
