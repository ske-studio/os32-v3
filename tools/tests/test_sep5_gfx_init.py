#!/usr/bin/env python3
"""B3: PC-98 planar bring-up provider keeps the pre-separation I/O and memory trace."""
CHECK = dict(name='sep5-gfx-init-host', tier=1, needs=[], budget=5,
    covers=['platform/pc98/gfx_init.c', 'kernel/console/display_restore.h',
            'gfx/gfx_core.c', 'gfx/gfx_core_init.inc', 'gfx/gfx_internal.h', 'gfx/gfx.h',
            'platform/powermac/display.c', 'tools/tests/sep5_gfx_init_host.c',
            'tools/tests/test_sep5_gfx_init.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk', 'include/**',
            'arch/x86/**', 'platform/pc98/**', 'lib/kstring.h', 'sdk/include/os32/**'])
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
# trace at run time. CORE_MUTATIONS reorder the common code's product text and
# must be reported by core_order() with exactly the named call.
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
# Order inside the common code (text of the product, comments removed).
CORE_ORDER = (
    ('gfx/gfx_core.c', '_gfx_common_init',
     ['display_text_hide();', 'kmemset(bb_b, 0, plane_sz);', 'kmemset(bb_i, 0, plane_sz);', 'display_planar16();']),
    ('gfx/gfx_core_init.inc', 'gfx_init_mode',
     ['_gfx_common_init(plane_sz);', 'display_planar_start(legacy200);', 'gfx_clear_planar_pages(plane_sz);',
      'display_flip_pages(0, 1);', 'palette_init();', 'gfx_scroll_init();']),
)


def execute(provider_text, work):
    (work / 'gfx_init_source.inc').write_text(provider_text)
    exe = work / 'gfx_init'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        *include_flags(ROOT), '-I' + str(work),
        str(ROOT / 'tools/tests/sep5_gfx_init_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def body(text, name):
    start = re.search(r'^static void ' + name + r'\([^\n]*\)\n\{', text, re.M)
    assert start, name
    pos, depth = start.end(), 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return re.sub(r'/\*.*?\*/', '', text[start.end():pos], flags=re.S)


CORE_MUTATIONS = (
    ('ff2-before-clear', 'gfx/gfx_core.c',
     '    display_text_hide();\n', '    display_text_hide();\n    display_planar16();\n',
     'gfx/gfx_core.c: display_planar16();'),
    ('flip-before-clear', 'gfx/gfx_core_init.inc',
     '    gfx_clear_planar_pages(plane_sz);\n', '    display_flip_pages(0, 1);\n    gfx_clear_planar_pages(plane_sz);\n',
     'gfx/gfx_core_init.inc: display_flip_pages(0, 1);'),
    ('raw-port-returns', 'gfx/gfx_core_init.inc',
     '    display_planar_start(legacy200);\n', '    display_planar_start(legacy200);\n    _out(0x68, 0x08);\n',
     'gfx/gfx_core_init.inc: raw PC-98 access _out( in gfx_init_mode'),
)


def core_order(override=None):
    """Returns the first violated expectation, or None."""
    for path, name, calls in CORE_ORDER:
        source = (override or {}).get(path) or (ROOT / path).read_text()
        text = body(source, name)
        at = -1
        for call in calls:
            if text.count(call) != 1 or text.index(call) < at:
                return path + ': ' + call
            at = text.index(call)
        for raw in ('_out(', 'TVRAM_', 'GDC_', 'MODE_FF'):
            if raw in text:
                return path + ': raw PC-98 access ' + raw + ' in ' + name
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    provider = (ROOT / PROVIDER).read_text()
    assert core_order() is None, core_order()
    with tempfile.TemporaryDirectory(prefix='sep5-gfx-init-') as directory:
        work = Path(directory)
        assert execute(provider, work) == 0
        print('PASS host32 planar start 200/400, flip pages, FF2, text hide bounds, page clear order; '
              'core calls the provider in the pre-separation order', flush=True)
        if args.mutate:
            for name, unit, old, new in MUTATIONS:
                assert provider.count(old) == 1, name
                rc = execute(provider.replace(old, new), work)
                assert rc == 1, (name, rc)
                print('MUTATE runtime RED ' + name, flush=True)
            for name, path, old, new, verdict in CORE_MUTATIONS:
                text = (ROOT / path).read_text()
                assert text.count(old) == 1, name
                assert core_order({path: text.replace(old, new)}) == verdict, name
                print('MUTATE order RED ' + name, flush=True)
            assert execute(provider, work) == 0


def run(ctx):
    import os
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_sep5_gfx_init.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    main()
