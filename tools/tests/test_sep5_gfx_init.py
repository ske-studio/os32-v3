#!/usr/bin/env python3
"""B3: PC-98 planar bring-up provider keeps the pre-separation I/O and memory trace."""
CHECK = dict(name='sep5-gfx-init-host', tier=1, needs=[], budget=5,
    covers=['platform/pc98/gfx_init.c', 'kernel/console/display_restore.h',
            'gfx/gfx_core.c', 'gfx/gfx_core_init.inc', 'gfx/gfx_internal.h', 'gfx/gfx.h',
            'platform/powermac/display.c', 'tools/tests/sep5_gfx_init_host.c',
            'tools/tests/test_sep5_gfx_init.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk', 'include/**',
            'arch/x86/**', 'platform/pc98/**', 'lib/kstring.h', 'sdk/include/os32/**',
            'gfx/palette.h', 'drivers/video/pc98/splash_raster.h', 'exec/surface_limits.h'])
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
PROVIDER = 'platform/pc98/gfx_init.c'
# (name, file, unique anchor, replacement): provider mutants must fail the host
# trace at run time.
MUTATIONS = (
    ('ff2-omitted', PROVIDER, '    _out(MODE_FF2_PORT, MFF2_16COLOR);', '    (void)0;'),
    ('start-first', PROVIDER,
     '    _out(GDC_GFX_CMD, GDC_GFX_400LINE);\n', '    _out(GDC_GFX_CMD, GDC_CMD_START);\n    _out(GDC_GFX_CMD, GDC_GFX_400LINE);\n'),
    ('200-param', PROVIDER, '_out(GDC_GFX_PARAM, legacy200 ? 0x01 : 0x00);', '_out(GDC_GFX_PARAM, 0x00);'),
    ('200-ff1', PROVIDER, 'legacy200 ? MFF1_200LINE : MFF1_HIRES', 'MFF1_HIRES'),
    ('flip-order', PROVIDER,
     '    _out(GDC_DISP_PAGE, display ? GDC_PAGE_1 : GDC_PAGE_0);\n    _out(GDC_ACCESS_PAGE, access ? GDC_PAGE_1 : GDC_PAGE_0);',
     '    _out(GDC_ACCESS_PAGE, access ? GDC_PAGE_1 : GDC_PAGE_0);\n    _out(GDC_DISP_PAGE, display ? GDC_PAGE_1 : GDC_PAGE_0);'),
    ('flip-access', PROVIDER, 'access ? GDC_PAGE_1 : GDC_PAGE_0', 'access ? GDC_PAGE_0 : GDC_PAGE_1'),
    ('hide-attr', PROVIDER, '        tvram_attr[i * 2] = 0x00;', '        tvram_attr[i] = 0x00;'),
    ('hide-short', PROVIDER, 'i < TVRAM_COLS * TVRAM_ROWS; i++', 'i < TVRAM_COLS * (TVRAM_ROWS - 1); i++'),
    ('clear-one-page', PROVIDER, '    _out(GDC_ACCESS_PAGE, GDC_PAGE_1);\n    kmemset', '    _out(GDC_ACCESS_PAGE, GDC_PAGE_0);\n    kmemset'),
)
CORE = {'core': 'gfx/gfx_core.c', 'init': 'gfx/gfx_core_init.inc'}
# The common callers are extracted from the product text and *executed* with
# recording fakes; these mutants must fail that executed trace.
CORE_MUTATIONS = (
    ('hide-before-plane-check', 'core',
     '    if (!bb_b || !bb_r || !bb_g || !bb_i) return;\n    dirty_queue.count = 0;\n',
     '    display_text_hide();\n    if (!bb_b || !bb_r || !bb_g || !bb_i) return;\n    dirty_queue.count = 0;\n'),
    ('ff2-before-clear', 'core',
     '    display_text_hide();\n\n', '    display_text_hide();\n    display_planar16();\n\n'),
    ('ff2-omitted-by-caller', 'core', '    kmemset(bb_i, 0, plane_sz);\n\n    display_planar16();\n',
     '    kmemset(bb_i, 0, plane_sz);\n'),
    ('flip-before-clear', 'init',
     '    gfx_clear_planar_pages(plane_sz);\n\n    /* ページ0を表示、ページ1に描画 */\n    display_flip_pages(0, 1);\n',
     '    display_flip_pages(0, 1);\n    gfx_clear_planar_pages(plane_sz);\n'),
    ('start-after-clear', 'init',
     '    display_planar_start(legacy200);\n\n    gfx_clear_planar_pages(plane_sz);\n',
     '    gfx_clear_planar_pages(plane_sz);\n    display_planar_start(legacy200);\n'),
    ('flip-pages-swapped', 'init', '    display_flip_pages(0, 1);', '    display_flip_pages(1, 0);'),
    ('200-clear-size', 'init', '    _gfx_common_init(plane_sz);', '    _gfx_common_init(GFX_PLANE_SZ);'),
)


def function(text, name):
    start = re.search(r'^static void ' + name + r'\([^\n]*\)\n\{', text, re.M)
    assert start, name
    pos, depth = start.end(), 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start.start():pos] + '\n'


def execute(texts, work):
    (work / 'gfx_init_source.inc').write_text(texts['provider'])
    (work / 'gfx_core_slice.inc').write_text(
        function(texts['core'], '_gfx_common_init') + function(texts['init'], 'gfx_init_mode'))
    exe = work / 'gfx_init'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        *include_flags(ROOT), '-I' + str(work),
        str(ROOT / 'tools/tests/sep5_gfx_init_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    texts = dict(provider=(ROOT / PROVIDER).read_text(),
                 **{key: (ROOT / path).read_text() for key, path in CORE.items()})
    with tempfile.TemporaryDirectory(prefix='sep5-gfx-init-') as directory:
        work = Path(directory)
        assert execute(texts, work) == 0
        print('PASS host32 provider trace (200/400 start, flip, FF2, text hide bounds, page clear) and '
              'executed common bring-up order (400, 200, missing plane)', flush=True)
        if args.mutate:
            for name, unit, old, new in (*((n, 'provider', o, r) for n, _, o, r in MUTATIONS),
                                         *CORE_MUTATIONS):
                assert texts[unit].count(old) == 1, name
                rc = execute(dict(texts, **{unit: texts[unit].replace(old, new)}), work)
                assert rc == 1, (name, rc)
                print('MUTATE runtime RED ' + name, flush=True)
            assert execute(texts, work) == 0


def run(ctx):
    import os
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_sep5_gfx_init.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    main()
