"""Run the diagnostic probe with bounded fake scan/clock and buffered output."""
CHECK = dict(name='splash-probe-host', tier=1,
    covers=['platform/pc98/splash_probe.c', 'drivers/video/pc98/splash_raster.c',
            'drivers/video/pc98/splash_raster.h', 'tools/tests/splash_probe_host.c',
            'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'include/platform_splash.h',
            'gfx/gfx_internal.h', 'gfx/gfx_osri.h', 'gfx/gfx.h', 'gfx/palette.h', 'include/gfx_hal.h',
            'include/pc98.h', 'include/pegc.h', 'include/types.h', 'include/sys.h',
            'mm/paging.h', 'drivers/char/serial.h', 'lib/serial_diag.h',
            'include/bootinfo.h', 'include/build_id.h', 'arch/x86/kernel/idt.h',
            'kernel/time/ktime.h', 'sdk/include/os32/os32_kapi_shared.h'], needs=[], budget=20.0)
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'platform/pc98/splash_probe.c'
MUTATIONS = (
    ('omit pattern', '    probe_pattern();', '    (void)probe_pattern;'),
    ('omit receive', '            probe_rx_count++;', '            (void)probe_rx_count;'),
    ('omit frame duration', '.duration = probe_now() - stamp,', '.duration = 0,'),
    ('print during scan', '    probe_pattern();', '    serial_puts_polled("early");\n    probe_pattern();'),
    ('omit Q measurement', 'if (kind == PROBE_Q) probe_poll_batch(count);',
     'if (kind == PROBE_Q) probe_poll_batch(1);'),
    ('mix clock into Q/W', 'c->total_us += elapsed;', 'c->total_us += elapsed + probe_c_min;'),
    ('omit clock batch', 'for (u32 i = 1; i < count; i++) (void)probe_now();',
     'for (u32 i = 1; i < 1; i++) (void)probe_now();'),
    ('omit W measurement', 'else for (u32 i = 0; i < count; i++) palette_set(1, 15, 0, 0);',
     'else for (u32 i = 0; i < count; i++) outp(PAL_IDX_PORT, 1);'),
    ('round cost bounds to us', '(c->upper_num_us * den + c->upper_samples - 1) / c->upper_samples;',
     'c->upper_us * den;'),
    ('omit cost IF0', 'unsigned int flags = irq_save();\n            unsigned long long span = probe_cost_batch(kind, count);',
     'unsigned int flags = _irq_enabled();\n            unsigned long long span = probe_cost_batch(kind, count);'),
    ('mix IF1 into upper bound', 'c->open_total_us += elapsed;',
     'c->open_total_us += elapsed;\n            if (elapsed * c->upper_samples > c->upper_num_us) c->upper_num_us = elapsed * c->upper_samples;'),
    ('omit measurement open window', 'probe_open_until = probe_now() + (s->line_num_us + s->line_den - 1) / s->line_den;',
     'probe_open_until = probe_now() + 0 * s->line_num_us;'),
    ('clock inside H sample run', 'for (u32 i = 1; i < count; i++) {\n            u32 status = inp(GDC_GFX_STAT);',
     'for (u32 i = 1; i < count; i++) {\n            (void)probe_now();\n            u32 status = inp(GDC_GFX_STAT);'),
    ('include partial H period', 'if (armed && completed < PROBE_HPERIODS)', 'if (completed < PROBE_HPERIODS)'),
    ('wrong duty numerator', '        highs += probe_highsamples[i];',
     '        highs += probe_hsamples[i] - probe_highsamples[i];'),
    ('charge C to B lower bound', 'lo - probe_budget.q.max : 0;', 'lo - probe_budget.c.max : 0;'),
    ('omit B upper sample error', '                                 probe_budget.q.max : 0;',
     '                                 0 : 0;'),
    ('extend H burst past 2H', 'u32 count = (2 * s->line_num_us - clocks) / q;',
     'u32 count = (4 * s->line_num_us - clocks) / q;'),
    ('lose VS classification', 'else if (probe_delta(measured, s->vs_lines * tolerance) <= tolerance)',
     'else if (0 && probe_delta(measured, s->vs_lines * tolerance) <= tolerance)'),
    ('ignore eight-period mismatch', 'expected * SPLASH_RASTER_PERIOD_TOLERANCE) return;',
     'expected * SPLASH_RASTER_PERIOD_TOLERANCE) (void)0;'),
    ('lose F bracket', 'probe_f_lo = last; probe_f_hi = after;', 'probe_f_lo = after; probe_f_hi = after;'),
    ('lose H bracket', 'probe_h_lo = last; probe_h_hi = after;', 'probe_h_lo = after; probe_h_hi = after;'),
    ('omit cumulative IRQ', 'e->irq_us.max += span - probe_c_min;', 'e->irq_us.max = span - probe_c_min;'),
    ('omit retry cost', 'return b->s[1].max - b->planned - b->retry;', 'return b->s[1].max - b->planned;'),
    ('lose negative margin', 'e->frame_j_max = initial_margin;', 'e->frame_j_max = 0;'),
    ('qualify unmeasured phase', '.initial_margin = initial_margin, .capture_margin = capture,',
     '.qualified = 1, .initial_margin = initial_margin, .capture_margin = capture,'),
    ('omit IF open window', 'probe_open_until = end + probe_open_gap_us;', 'probe_open_until = end;'),
)
def ordinary():
    with tempfile.TemporaryDirectory(prefix='splash-probe-ordinary-') as td:
        obj = Path(td) / 'probe.o'
        cross = Path(os.environ['CROSS_DIR']) / 'bin'
        subprocess.run([str(cross / 'i386-elf-gcc'), '-std=gnu11', '-O2',
            '-ffreestanding', '-c', str(ROOT / SOURCE), '-o', str(obj)], check=True)
        assert not subprocess.check_output([str(cross / 'i386-elf-nm'), str(obj)]).strip()
    print('GREEN ordinary build contains no probe code/storage', flush=True)
def execute(mutation=None):
    with tempfile.TemporaryDirectory(prefix='splash-probe-') as td:
        work = Path(td)
        for rel in (SOURCE, 'drivers/video/pc98/splash_raster.c', 'tools/tests/splash_probe_host.c'):
            dest = work/rel; dest.parent.mkdir(parents=True, exist_ok=True)
            source = (ROOT/rel).read_text()
            if rel == SOURCE and mutation:
                name, old, new = mutation
                assert source.count(old) == 1, name
                source = source.replace(old, new, 1)
            dest.write_text(source)
        cc = str(Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc'); exe = work/'probe'
        subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
            '-D__KERNEL_BUILD__', '-DSPLASH_RASTER_DIAG', *include_flags(ROOT),
            '-I'+str(ROOT/'drivers/video/pc98'), str(work/'tools/tests/splash_probe_host.c'),
            '-lgcc', '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True)
        if not mutation and result.stdout: print(result.stdout.decode(), end='', flush=True)
        if result.returncode: print('runtime rc=', result.returncode, result.stderr, flush=True)
        return result.returncode

def main():
    p = argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true'); args = p.parse_args()
    ordinary()
    assert execute() == 0
    print('GREEN four wave types, independent IN/clock/OUT costs, cumulative IRQ/J_max, signed margins, buffered output, bounded failure', flush=True)
    if args.mutate:
        for mutation in MUTATIONS:
            rc = execute(mutation)
            assert rc != 0, mutation[0]
            print(f'RED {mutation[0]}: compiled, runtime rc={rc}', flush=True)
def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_splash_probe.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
if __name__ == '__main__': main()
