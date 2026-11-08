"""L4: text drawing through DIRECT commands and the SHADOW path."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

CHECK = dict(name='gfx-text-host', tier=1, needs=[], budget=15, covers=[
    'userland/lib/gfx/text/gfx_font.c', 'userland/lib/gfx/text/gfx_kcg.c',
    'userland/lib/gfx/draw/gfx_cmd.c', 'userland/lib/gfx/gfx_direct_internal.h',
    'userland/lib/gfx/gfx_font.h', 'userland/lib/gfx/libos32gfx.h',
    'tools/tests/gfx_text_host.c', 'tools/tests/test_gfx_text.py',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('font-direct', 'userland/lib/gfx/text/gfx_font.c', '    if (gfx_direct) {\n        u8 bits[8 * 80];', '    if (0) {\n        u8 bits[8 * 80];'),
    ('font-color', 'userland/lib/gfx/text/gfx_font.c', 'color, -1, cx, y, n * FONT_W, FONT_H);', 'color + 1, -1, cx, y, n * FONT_W, FONT_H);'),
    ('kcg-direct', 'userland/lib/gfx/text/gfx_kcg.c', '    if (gfx_direct) {\n        u8 bits[80 * 64];', '    if (0) {\n        u8 bits[80 * 64];'),
    ('ank-direct', 'userland/lib/gfx/text/gfx_kcg.c', '    if (gfx_direct) {\n        gfx_api->kcg_read_ank(ch, pat);', '    if (0) {\n        gfx_api->kcg_read_ank(ch, pat);'),
    ('kanji-direct', 'userland/lib/gfx/text/gfx_kcg.c', '    if (gfx_direct) {\n        gfx_api->kcg_read_kanji(jis_code, pat);', '    if (0) {\n        gfx_api->kcg_read_kanji(jis_code, pat);'),
    ('kcg-boundary', 'userland/lib/gfx/text/gfx_kcg.c', 'if (width + glyph_w * scale > 640)', 'if (width + glyph_w * scale >= 640)'),
    ('kcg-color', 'userland/lib/gfx/text/gfx_kcg.c', '        if (width) (void)gfx_cmd_enqueue_mono(start, y, width, 16 * scale, bits, 80,\n            fg, bg == 0xFF ? -1 : bg, start, y, width, 16 * scale);', '        if (width) (void)gfx_cmd_enqueue_mono(start, y, width, 16 * scale, bits, 80,\n            fg + 1, bg == 0xFF ? -1 : bg, start, y, width, 16 * scale);'),
)


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
               *include_flags(ROOT), "-I" + str(ROOT / "userland/lib/gfx"),
               str(ROOT / 'tools/tests/gfx_text_host.c'),
               str(ROOT / 'userland/lib/gfx/text/gfx_font.c'),
               str(ROOT / 'userland/lib/gfx/text/gfx_kcg.c'),
               str(ROOT / 'userland/lib/gfx/draw/gfx_cmd.c'), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), result
    if mutation:
        assert result.stdout.startswith('FAIL '), result
    print(('MUTATE RED ' + mutation[0]) if mutation else 'PASS L4 text (' + runner + ')')


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
    argv = ['python3', '-B', 'tools/tests/test_gfx_text.py', *(['--mutate'] if ctx.mutate else [])]
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
