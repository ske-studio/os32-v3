#!/usr/bin/env python3
"""Trident depth DAC tables, shadow/reload, overscan and I/O failures."""
CHECK = dict(name='trident-palette', tier=1, needs=[], budget=3,
    covers=['drivers/video/trident/palette.c', 'drivers/video/trident/trident.h',
            'tools/tests/trident_palette_host.c', 'tools/tests/trident_device_fake.c',
            'tools/tests/trident_device_fake.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk', 'gfx/palette.h'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = ROOT / 'drivers/video/trident/palette.c'
MUTATIONS = (
    ('last_color', 'count > (int)TRI_DAC_COLORS - first', 'count >= (int)TRI_DAC_COLORS - first'),
    ('mask', 'TRI_DAC_MASK, 0xFF', 'TRI_DAC_MASK, 0xFE'),
    ('index', '(u8)(first + i)', '(u8)(first + i + 1)'),
    ('conversion', 'trident_palette_dac6(*rgb++, GFX_PALETTE_COMPONENT_BITS)', '(*rgb++ & GFX_PALETTE_COMPONENT_MASK)'),
    ('delay_base', 'return trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6, &status);', 'return trident_read8(d, TRI_SPACE_REGS, TRI_CR_COLOR + 6, &status);'),
    ('delay_rc', 'if (rc) return rc;\n    return trident_read8', 'if (rc) return 0;\n    return trident_read8'),
    ('delay_count', 'return trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6, &status);', 'return TRI_OK;'),
    ('record', 'd->dac[first + i][c] = dac6[i * GFX_RGB_COMPONENTS + c];', 'd->dac[first + i][c] = dac6[i * GFX_RGB_COMPONENTS + c] ^ 1;'),
    ('record_early', 'const u8 *color = dac6 + i * GFX_RGB_COMPONENTS;',
     'const u8 *color = dac6 + i * GFX_RGB_COMPONENTS; tri_palette_record(d, first + i, 1, color);'),
    ('green565', 'rgb[i * 3 + 1] = i;', 'rgb[i * 3 + 1] = i * 63 / 31;'),
    ('unused565', 'i * PALETTE_DAC_MAX / PALETTE_RGB5_MAX : 0;', 'i * PALETTE_DAC_MAX / PALETTE_RGB5_MAX : 63;'),
    ('ramp555', 'rgb[i * 3 + c] = i * PALETTE_DAC_MAX / PALETTE_RGB5_MAX;', 'rgb[i * 3 + c] = i;'),
    ('ramp888', 'rgb[i * 3 + c] = i * PALETTE_DAC_MAX / PALETTE_RGB8_MAX;', 'rgb[i * 3 + c] = i & 63;'),
    ('truecolor_store', 'case 15: case 16: case 24: return TRI_OK;', 'case 15: case 16: case 24: break;'),
    ('reload_saved', 'if (d->overscan_valid) return tri_overscan(d, d->overscan);', 'if (d->overscan_valid) return tri_overscan(d, 0);'),
    ('reload_index8', 'palette_write_dac6(d, 0, TRI_DAC_COLORS, &d->dac[0][0])', 'palette_write_dac6(d, 0, TRI_DAC_COLORS - 1, &d->dac[0][0])'),
    ('overscan_sum', 'color[0] + color[1] + color[2]', 'color[0]'),
    ('overscan_reverse', 'i = (int)TRI_DAC_COLORS - 1; i >= 0; --i', 'i = 0; i < (int)TRI_DAC_COLORS; ++i'),
    ('overscan_changed', '(overscan >= first && overscan < first + count)', '(overscan >= first && overscan < first)'),
    ('overscan_black', 'else if (palette_color_sum(d->dac[overscan]) != 0)', 'else if (1)'),
    ('overscan_strict', 'if (palette_color_sum(d->dac[i]) <\n                    palette_color_sum(d->dac[candidate]))\n                    candidate = i;\n            }\n            if (i == first + count)',
     'if (palette_color_sum(d->dac[i]) <=\n                    palette_color_sum(d->dac[candidate]))\n                    candidate = i;\n            }\n            if (i == first + count)'),
    ('overscan_enable', 'TRI_AR_INDEX, PALETTE_AR_ENABLE', 'TRI_AR_INDEX, PALETTE_AR_DISABLE'),
)

def execute(source, work):
    exe = work / 'palette-test'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-no-pie', '-I' + str(ROOT / 'drivers/video/trident'), '-I' + str(ROOT / 'tools/tests'), '-I' + str(ROOT), *include_flags(ROOT), str(source), str(ROOT / 'tools/tests/trident_device_fake.c'), str(ROOT / 'tools/tests/trident_palette_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='trident-palette-') as tmp:
        work = Path(tmp)
        assert execute(PRODUCT, work) == 0
        print('PASS depth tables, DAC trace, record/reload, overscan and errors', flush=True)
        if args.mutate:
            original = PRODUCT.read_text()
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, name
                source = work / 'palette.c'
                source.write_text(original.replace(old, new))
                rc = execute(source, work)
                assert rc == 1, (name, rc)
                print(f'MUTATE RED {name} rc={rc}', flush=True)
            assert execute(PRODUCT, work) == 0
            print('PASS restored production source', flush=True)
    return 0

def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_palette.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)

if __name__ == '__main__':
    raise SystemExit(main())
