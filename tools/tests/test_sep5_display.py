#!/usr/bin/env python3
"""B1: legacy palette state and fixed pre-separation PC-98 I/O trace."""
CHECK = dict(name='sep5-display-host', tier=1, needs=[], budget=5,
    covers=['gfx/palette.c', 'kernel/console/display_restore.h',
            'platform/pc98/display.c', 'platform/powermac/display.c',
            'build/platform/powermac.mk', 'tools/tests/sep5_display_host.c',
            'tools/tests/test_sep5_display.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk',
            'gfx/palette.h', 'include/**', 'arch/x86/**', 'gfx/gfx_internal.h',
            'gfx/gfx.h', 'platform/pc98/**', 'drivers/video/pc98/pc98_route.h',
            'sdk/include/os32/**', 'exec/surface_limits.h'])
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('GR-order', 'display', 'outp(PAL_G_PORT, g & GFX_PALETTE_COMPONENT_MASK);\n    outp(PAL_R_PORT, r & GFX_PALETTE_COMPONENT_MASK);',
     'outp(PAL_R_PORT, r & GFX_PALETTE_COMPONENT_MASK);\n    outp(PAL_G_PORT, g & GFX_PALETTE_COMPONENT_MASK);'),
    ('provider-mask', 'display', 'outp(PAL_B_PORT, b & GFX_PALETTE_COMPONENT_MASK);', 'outp(PAL_B_PORT, b);'),
    ('index', 'display', 'outp(PAL_IDX_PORT, (u8)index);', 'outp(PAL_IDX_PORT, (u8)(index + 1));'),
    ('set-mask', 'palette', 'shadow[idx].r = r & GFX_PALETTE_COMPONENT_MASK;\n    shadow[idx].g = g & GFX_PALETTE_COMPONENT_MASK;\n    shadow[idx].b = b & GFX_PALETTE_COMPONENT_MASK;\n    display_palette4_write',
     'shadow[idx].r = r;\n    shadow[idx].g = g;\n    shadow[idx].b = b;\n    display_palette4_write'),
    ('shadow-only-io', 'palette', 'void palette_shadow_set(int idx, u8 r, u8 g, u8 b)\n{',
     'void palette_shadow_set(int idx, u8 r, u8 g, u8 b)\n{\n    display_palette4_write(idx, r, g, b);'),
    ('shadow-only-omitted', 'palette', 'void palette_shadow_set(int idx, u8 r, u8 g, u8 b)\n{',
     'void palette_shadow_set(int idx, u8 r, u8 g, u8 b)\n{\n    return;'),
    ('init-color', 'palette', '{  0,  0,  7 },', '{  0,  0,  6 },'),
    ('set-bounds', 'palette', 'void palette_set(int idx, u8 r, u8 g, u8 b)\n{\n    if (idx < 0 || idx >= GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP)) return;',
     'void palette_set(int idx, u8 r, u8 g, u8 b)\n{\n    if (idx < 0 || idx >= GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP) - 1) return;'),
)


def execute(texts, work):
    for name, text in texts.items():
        (work / (name + '_source.inc')).write_text(text)
    exe = work / 'display'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        *include_flags(ROOT), '-I' + str(work),
        str(ROOT / 'tools/tests/sep5_display_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    texts = dict(palette=(ROOT/'gfx/palette.c').read_text(),
                 display=(ROOT/'platform/pc98/display.c').read_text())
    with tempfile.TemporaryDirectory(prefix='sep5-display-') as directory:
        work = Path(directory)
        assert execute(texts, work) == 0
        print('PASS host32 palette defaults/bounds/masks/shadow-only and fixed index/G/R/B trace', flush=True)
        if args.mutate:
            for name, unit, old, new in MUTATIONS:
                assert texts[unit].count(old) == 1, name
                changed = dict(texts)
                changed[unit] = texts[unit].replace(old, new)
                rc = execute(changed, work)
                assert rc == 1, (name, rc)
                print('MUTATE runtime RED ' + name, flush=True)
            assert execute(texts, work) == 0


def run(ctx):
    import os
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_sep5_display.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    main()
