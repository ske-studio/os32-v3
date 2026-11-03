"""Shared palette player: rational scan model, IRQ windows and runtime mutants."""
CHECK = dict(name='splash-raster-host', tier=1,
    covers=['drivers/video/pc98/splash_raster.c', 'drivers/video/pc98/splash_raster_image.c', 'drivers/video/pc98/splash_raster.h',
            'tools/tests/splash_raster_host.c', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'gfx/gfx_osri.h', 'gfx/gfx.h',
            'include/types.h', 'include/pc98.h', 'include/pegc.h', 'include/sys.h', 'gfx/palette.h',
            'sdk/include/os32/os32_kapi_shared.h'],
    needs=[], budget=15.0)

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'drivers/video/pc98/splash_raster.c'
HARNESS = 'tools/tests/splash_raster_host.c'
MUTATIONS = (
    ('D1 contaminates mode', 'inp(PEGC_HSYNC_PORT) & PEGC_HSYNC_READ_HF',
     'inp(PEGC_HSYNC_PORT) & PEGC_HSYNC_MASK'),
    ('reject 24k', 's->dynamic_ok = 1;', 's->dynamic_ok = s->mode;'),
    ('skip period comparison', 'delta * PERCENT > expected * PERIOD_TOLERANCE',
     'delta * PERCENT > expected * PERCENT'),
    ('wrong VS phase', 's->active0_lines = s->vbp_lines;', 's->active0_lines = 0;'),
    ('accept unknown high width', 'delta > (unsigned long long)MEASURE_PERIODS * s->line_num_us',
     'delta > (unsigned long long)MEASURE_PERIODS * expected'),
    ('ignore line', 's->active0_lines + e->line', 's->active0_lines + 4'),
    ('relative 120us', 'unsigned long long start = target - lines_us(s, 1);',
     'unsigned long long start = fall + (unsigned long long)i * 120;'),
    ('accept sub-two-line gap', 'SPLASH_RASTER_MIN_GAP))', '0))'),
    ('omit line-zero color', 'color(e->pal_idx, e->r, e->g, e->b);\n        DIAG(diag_event_end(0));\n        first = 1;',
     '(void)e;\n        first = 1;'),
    ('omit fixed initial color', 'color(i, rgb >> 8, rgb >> 4, rgb);', '(void)rgb;'),
    ('forget reset', 'raster = (struct SplashRaster){0};', 'raster.frame = 0;'),
    ('lose lazy measurement latch', 's->checked = 1;', 's->checked = 0;'),
    ('latch measurement with IF disabled',
     'if (!_irq_enabled()) return result(s, SPLASH_RASTER_NO_SYNC);',
     'if (!_irq_enabled()) { s->checked = 1; return result(s, SPLASH_RASTER_NO_SYNC); }'),
    ('lose IRQ restoration', 'irq_restore(flags);\n            if (now_us(&now))',
     '(void)flags;\n            if (now_us(&now))'),
    ('lose IRQ open window', 'if (wait_until(open_until, &now))', 'if (wait_until(now, &now))'),
    ('stop on late', 'if (applied) break;', 'if (late) return result(s, SPLASH_RASTER_NO_SYNC);\n            if (applied) break;'),
    ('omit late frame discard', 's->dropped++;', 's->frame++;'),
    ('accept initially low VSYNC', 'if (!(inp(GDC_GFX_STAT) & GDC_STAT_VSYNC))',
     'if (0)'),
    ('apply discarded frame', 's->dropped++;\n    return 1;',
     's->dropped++;\n    *fall = predicted;\n    *open_until = predicted;\n    return 0;'),
    ('ignore entry IF', 'if (!_irq_enabled() || !s->dynamic_ok', 'if (!s->dynamic_ok'),
    ('unbounded stopped clock', 'poll < POLL_LIMIT; poll++', '1; poll++'),
    ('allow extra deadline poll', '(*stamp == deadline && !!(status & bit) != high)',
     '(0 && !!(status & bit) != high)'),
    ('remove VSYNC timeout', '*stamp > deadline ||\n            (*stamp == deadline && !!(status & bit) != high)',
     '(bit == GDC_STAT_HBLANK && *stamp >= deadline)'),
)


def build(work, source):
    for rel in (SOURCE, HARNESS):
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if rel == SOURCE:
            dest.write_text(source)
        else:
            shutil.copyfile(ROOT / rel, dest)
    cross_dir = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))
    cc = cross_dir / 'bin/i386-elf-gcc'
    exe = work / 'raster'
    subprocess.run([str(cc), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                    '-D__KERNEL_BUILD__', *include_flags(ROOT),
                    '-I' + str(ROOT / 'drivers/video/pc98'),
                    str(work / HARNESS), '-lgcc', '-o', str(exe)], check=True)
    return exe


def execute(source):
    with tempfile.TemporaryDirectory(prefix='splash-raster-') as td:
        exe = build(Path(td), source)
        completed = host32.run([str(exe)], runner='qemu', capture_output=True)
        if completed.returncode:
            print(completed.stderr.decode() if isinstance(completed.stderr, bytes) else completed.stderr, flush=True)
        return completed.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / SOURCE).read_text()
    rc = execute(source)
    assert rc == 0, f'baseline runtime rc={rc} (REQUIRE line modulo 250 + 1)'
    print('GREEN both modes/phase types, +/-1%, absolute lines, IRQ, timeout, late/recovery, fixed palette, deferred IF=0 measurement, missed falling edge', flush=True)
    if args.mutate:
        for name, old, new in MUTATIONS:
            # Two loops intentionally share a bound: mutate one unique whole line.
            if name == 'unbounded stopped clock':
                old = 'for (u32 poll = 0; poll < POLL_LIMIT; poll++) {\n        u32 status'
                new = 'for (u32 poll = 0; 1; poll++) {\n        u32 status'
            assert source.count(old) == 1, f'ambiguous/missing anchor: {name}'
            changed = source.replace(old, new, 1)
            rc = execute(changed)
            assert rc != 0, f'survived: {name}'
            print(f'RED {name}: compiled, runtime rc={rc}', flush=True)


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_splash_raster.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
