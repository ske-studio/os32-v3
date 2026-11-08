"""Run the diagnostic probe with bounded fake scan/clock and buffered output."""
CHECK = dict(name='splash-probe-host', tier=1,
    covers=['platform/pc98/bootinfo_wire.h', 'platform/pc98/bootinfo_native.h', 'platform/pc98/splash_probe.c', 'drivers/video/pc98/splash_raster.c',
            'drivers/video/pc98/splash_raster.h', 'tools/tests/splash_probe_host.c',
            'drivers/video/pc98/splash_raster_image.h',
            'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'include/platform_splash.h',
            'platform/pc98/pit.h', 'platform/pc98/pit_math.h',
            'include/time_units.h', 'include/memmap.h',
            'gfx/gfx_internal.h', 'gfx/gfx_osri.h', 'gfx/gfx.h', 'gfx/palette.h', 'include/gfx_hal.h',
            'include/pc98.h', 'include/pegc.h', 'include/types.h', 'include/sys.h',
            'mm/paging.h', 'platform/pc98/legacy/v86_bios.h', 'drivers/char/serial.h', 'lib/serial_diag.h',
            'include/bootinfo.h', 'include/build_id.h', 'include/config.h', 'arch/x86/kernel/idt.h',
            'kernel/time/ktime.h', 'sdk/include/os32/os32_kapi_shared.h'], needs=[],
    # Reduced host workload measured 13.13 s locally; allow over 2x headroom.
    budget=51.6)
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'platform/pc98/splash_probe.c'
MUTATIONS = (
    ('include failed phase column budget',
     '            if ((long long)(2 * one) > transition) transition = 2 * one;',
     '            if ((long long)(2 * one) > transition) transition = 2 * one;\n'
     '            if (c->w > r->budget.w.max) r->budget.w.max = c->w;\n'
     '            if (c->d > r->budget.d.max) r->budget.d.max = c->d;\n'
     '            if (c->prep > r->prep) r->prep = c->prep;\n'
     '            if (transition > r->transition) r->transition = transition;'),
    ('omit joint phase error status',
     'probe_errors != errors_before ? PROBE_SCAN_ERROR :\n                probe_errors ? PROBE_SCAN_AFTER_ERROR : PROBE_SCAN_COMPLETE;',
     'probe_errors != errors_before ? PROBE_SCAN_AFTER_ERROR :\n                probe_errors ? PROBE_SCAN_AFTER_ERROR : PROBE_SCAN_COMPLETE;'),
    ('omit first error reason output', 'probe_field("SPLR error_first reason=", probe_error_first.reason);',
     'probe_field("SPLR missing_first reason=", probe_error_first.reason);'),
    ('abort after column error', '        c->scan_reason = PROBE_SCAN_ERROR;',
     '        c->scan_reason = PROBE_SCAN_ERROR; return;'),
    ('omit UART initialization', '    serial_init(SYS_SERIAL_BAUD);',
     '    (void)SYS_SERIAL_BAUD;'),
    ('direct BIOS read before begin',
     'probe_bios_before = v86_bios_real_lowmem()[PEGC_BIOS_PRXCRT];',
     'probe_bios_before = *(volatile u8 *)P2V_IO(PEGC_BIOS_PRXCRT);'),
    ('direct BIOS read after begin',
     'u32 bios_after = v86_bios_real_lowmem()[PEGC_BIOS_PRXCRT];',
     'u32 bios_after = *(volatile u8 *)P2V_IO(PEGC_BIOS_PRXCRT);'),
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
    ('clock inside H sample run', '        probe_hstatus[i] = inp(GDC_GFX_STAT);',
     '        probe_hstatus[i] = inp(GDC_GFX_STAT) + 0 * probe_now();'),
    ('aggregate inside IF0',
     '        unsigned long long elapsed = probe_now() - a;\n        probe_measure_restore(flags, s);\n        completed = probe_hcollect(count, completed);',
     '        completed = probe_hcollect(count, completed);\n        unsigned long long elapsed = probe_now() - a;\n        probe_measure_restore(flags, s);'),
    ('include partial H period', 'if (armed && completed < PROBE_HPERIODS)', 'if (completed < PROBE_HPERIODS)'),
    ('wrong duty numerator', '        highs += probe_highsamples[i];',
     '        highs += probe_hsamples[i] - probe_highsamples[i];'),
    ('charge C to B lower bound', 'lo - sample_q : 0;', 'lo - probe_budget.c.max : 0;'),
    ('omit B upper sample error', '                                 sample_q : 0;',
     '                                 0 : 0;'),
    ('restore bootstrap B sample error', 'long long sample_q = probe_hd.cal[1].q_after;',
     'long long sample_q = probe_cost_bound(PROBE_Q, den);'),
    ('omit batch peak collection', '        probe_pl_peak_add(peak, hi);',
     '        (void)peak;'),
    ('lose maximum frequency', 'else if (value == p->max) p->hits++;',
     'else if (value == p->max) p->hits = 1;'),
    ('count former maximum hits', '        p->hits = 1;', '        p->hits++;'),
    ('lose former maximum as second', 'p->second = p->max; p->second_valid = 1;',
     'p->second = value; p->second_valid = 1;'),
    ('lose intermediate second value', 'value > p->second)', 'value < p->second)'),
    ('mix Q seed into batch distribution', '        peak->seed = *one_upper;',
     '        peak->seed = *one_upper; probe_pl_peak_add(peak, *one_upper);'),
    ('omit W peak subtraction', 'long long subtract = k == PROBE_PL_W ? c->empty_min[phase] : 0;',
     'long long subtract = 0;'),
    ('omit peak dump frequency', 'probe_field(" max_count=", p->hits);',
     'probe_field(" missing_max_count=", p->hits);'),
    ('omit Q detail raw collection', '    probe_pl_peak_add(&d->raw, raw);',
     '    (void)raw;'),
    ('omit Q detail corrected collection', '    probe_pl_peak_add(&d->upper, corrected);',
     '    (void)corrected;'),
    ('lose signed clock correction', 'probe_pl_peak_add(&d->correction, corrected - raw);',
     'probe_pl_peak_add(&d->correction, 0);'),
    ('omit single seed detail', '                            seed, 1, s->line_den, 1);',
     '                            0, 1, s->line_den, 1);'),
    ('omit transition prep', 'const struct PlRow *r = probe_pl_prepare(c, phase + 1);',
     'const struct PlRow *r = &c->row[(phase + 1) & 1];'),
    ('omit next wait IN', '1, 2, 0, 0, &iterations, &next);',
     '1, 1, 0, 0, &iterations, &next);'),
    ('clock inside successful edge loop', 'u32 status = inp(GDC_GFX_STAT);\n        if (i >= upper)',
     'u32 status = inp(GDC_GFX_STAT); (void)probe_now();\n        if (i >= upper)'),
    ('force first-IN success', '0, 0, &iterations, &first)) probe_error(PROBE_ERR_PL_SUCCESS, iterations);',
     '1, 0, &iterations, &first)) probe_error(PROBE_ERR_PL_SUCCESS, iterations);'),
    ('lose outside entry cost', 'first.begin - begin + end - first.end;',
     'end - first.end;'),
    ('lose outside return cost', 'first.begin - begin + end - first.end;',
     'first.begin - begin;'),
    ('one clock for disjoint outside intervals', 'probe_pl_batch_outside, 1, s->line_den, 2);',
     'probe_pl_batch_outside, 1, s->line_den, 1);'),
    ('charge body to linkage', 'next.begin - first.end;', 'next.begin - first.begin;'),
    ('omit link budget comparison', 'probe_signed(" link_budget_num=", r->prep + r->transition);',
     'probe_signed(" link_budget_num=", r->transition);'),
    ('omit outside output', 'probe_field("SPLR pl_joint_outer_n=", n);',
     'probe_field("SPLR missing_joint_outer_n=", n);'),
    ('omit joint real IN', 'u32 status = inp(GDC_GFX_STAT);\n        if (i >= upper)',
     'u32 status = 0;\n        if (i >= upper)'),
    ('omit joint upper guard', 'if (i >= upper) { *iterations = i; break; }',
     'if (0 && i >= upper) { *iterations = i; break; }'),
    ('omit joint lower guard', '            if (i < lower) break;',
     '            if (0 && i < lower) break;'),
    ('omit joint emit', '            emit(r);\n            result = 0;',
     '            (void)r; (void)emit;\n            result = 0;'),
    ('omit joint count six', 'n < PROBE_PL_SLOTS && !probe_fatal;',
     'n < PROBE_PL_SLOTS - 1 && !probe_fatal;'),
    ('invalid N3 representative', '        if (n == 3) {', '        if (0) {'),
    ('omit joint output', 'probe_field("SPLR pl_joint_n=", n);',
     'probe_field("SPLR missing_joint_n=", n);'),
    ('omit cycle high count', 'probe_field(" ", probe_highsamples[i]); probe_field(":", probe_hsamples[i]);',
     'probe_field(" ", 0); probe_field(":", probe_hsamples[i]);'),
    ('omit cycle total count', 'probe_field(" ", probe_highsamples[i]); probe_field(":", probe_hsamples[i]);',
     'probe_field(" ", probe_highsamples[i]); probe_field(":", 0);'),
    ('omit B cycle dump', '    probe_hsamples_dump();', '    (void)probe_hsamples_dump;'),
    ('omit Q details dump', '    probe_pl_details_dump();', '    (void)probe_pl_details_dump;'),
    ('truncate continuous H capture',
     'u32 count = ((PROBE_HPERIODS + 1) * s->line_num_us + q + probe_q_lower - 1) / probe_q_lower + 1;',
     'u32 count = s->line_num_us / q;'),
    ('coarse Q calibration for 64H', 'u32 count = (limit - clocks) / *q;',
     'u32 count = 8;'),
    ('ignore fixed costs in lower window', 'long long remaining = r->early_time - fixed;',
     'long long remaining = r->early_time;'),
    ('use shortest poll for lower window',
     'r->lower = remaining > 0 ? (remaining + r->poll.max - 1) / r->poll.max : 1;',
     'r->lower = remaining > 0 ? (remaining + r->poll.min - 1) / r->poll.min : 1;'),
    ('restore 66H capture limit', 'unsigned long long limit = (PROBE_HPERIODS + 6) * s->line_num_us;',
     'unsigned long long limit = (PROBE_HPERIODS + 2) * s->line_num_us;'),
    ('extend IF0 time frame', 'unsigned long long limit = (PROBE_HPERIODS + 6) * s->line_num_us;',
     'unsigned long long limit = (PROBE_HPERIODS + 32) * s->line_num_us;'),
    ('ignore H sample count cap',
     'if (count > cap) { probe_hd.reason = 3; probe_hd.budget_stage = 5; probe_error(PROBE_ERR_H_COUNT, count); return 0; }',
     'if (count > cap + PROBE_HSAMPLES) { probe_hd.reason = 3; probe_hd.budget_stage = 5; probe_error(PROBE_ERR_H_COUNT, count); return 0; }'),
    ('omit second H precision pass', 'for (u32 pass = 0; pass < 2; pass++)',
     'for (u32 pass = 0; pass < 1; pass++)'),
    ('use one rounded short sandwich', 'sample < PROBE_COST_BATCH;', 'sample < 1;'),
    ('short PL Q calibration', 'if (count > PROBE_COST_BATCH) count = PROBE_COST_BATCH;',
     'if (count > PROBE_BURST) count = PROBE_BURST;'),
    ('omit budget rejection substage', 'probe_hd.budget_stage = 5;', 'probe_hd.budget_stage = 0;'),
    ('omit retained cap', 'probe_hd.cap = cap;', 'probe_hd.cap = 0;'),
    ('omit retained excess',
     'probe_hd.excess = (long long)(count * q + clocks) - (long long)limit;',
     'probe_hd.excess = 0;'),
    ('omit calibration span minimum', 'c->span_min = span;', 'c->span_min = 0;'),
    ('omit calibration span maximum', 'c->span_max = span;', 'c->span_max = 0;'),
    ('omit H budget dump', 'probe_signed(" excess_num=", probe_hd.excess);',
     'probe_signed(" missing_excess_num=", probe_hd.excess);'),
    ('omit column skip reason', 'c->scan_reason = 2;', 'c->scan_reason = 0;'),
    ('omit column skip dump', 'probe_field(" scan_reason=", c->scan_reason);',
     'probe_field(" missing_scan_reason=", c->scan_reason);'),
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
    ('omit H rise diagnostic', 'probe_hd.rise++;', '(void)probe_hd.rise;'),
    ('omit H fall diagnostic', 'probe_hd.fall++;', '(void)probe_hd.fall;'),
    ('omit H first sample', 'if (old & GDC_STAT_HBLANK) probe_hd.high++; else probe_hd.low++;', '(void)old;'),
    ('omit H completed diagnostic', 'probe_hd.completed = completed;', 'probe_hd.completed = 0;'),
    ('omit H min time', 'probe_hd.min_us = elapsed;', 'probe_hd.min_us = 0;'),
    ('omit H max time', 'probe_hd.max_us = elapsed;', 'probe_hd.max_us = 0;'),
    ('omit H overrun', 'probe_hd.over++; probe_hd.reason = 8;', 'probe_hd.reason = 8;'),
    ('wrong H reason', 'completed == PROBE_HPERIODS ? 1 : 6;', 'completed == PROBE_HPERIODS ? 1 : 9;'),
    ('omit N6 fixed columns', 'n <= SPLASH_RASTER_IMAGE_DIAG_OUTS;', 'n < SPLASH_RASTER_IMAGE_DIAG_OUTS;'),
    ('omit raw block', 'block < PROBE_RAW_BLOCKS;', 'block < PROBE_RAW_BLOCKS - 1;'),
    ('omit raw read', 'i < PROBE_RAW_READS;', 'i < PROBE_RAW_READS - 1;'),
    ('wrong raw port', 'u32 value = inp(port);', 'u32 value = inp(GDC_GFX_STAT); (void)port;'),
    ('raw CLI', '*r = (struct probe_raw){ .both = PROBE_RAW_VALUES - 1 };',
     '*r = (struct probe_raw){ .both = PROBE_RAW_VALUES - 1 }; (void)irq_save();'),
    ('raw inner clock', 'r->freq[value]++;', 'r->freq[value]++; (void)probe_now();'),
    ('omit raw AND', 'r->both &= value;', 'r->both |= value;'),
    ('omit raw OR', 'r->either |= value;', 'r->either &= value;'),
    ('omit raw frequency', 'r->freq[value]++;', 'r->freq[value] += 2;'),
    ('omit raw high', 'if (level) b->high++; else b->low++;', 'if (level) b->high += 0; else b->low++;'),
    ('omit raw edge', 'if (level) b->rise++; else b->fall++;', 'if (level) b->rise += 0; else b->fall++;'),
    ('wrong raw run minimum', 'run < b->min[level]', 'run > b->min[level]'),
    ('omit raw run tail', 'probe_raw_run(&r->bit[k], (old >> (5 + k)) & 1, run[k]);', '(void)run[k];'),
    ('omit raw elapsed', 'r->elapsed += probe_now() - begin;', 'r->elapsed = probe_now() - begin;'),
    ('print zero bins', 'if (r->freq[value]) {', 'if (1) {'),
    ('unbounded histogram lines', '#define PROBE_RAW_PER_LINE 16u', '#define PROBE_RAW_PER_LINE 1u'),
    ('omit new dump', '    probe_measure_dump();', '    (void)probe_measure_dump;'),
    ('omit raw scan after H failure', '    probe_raw_scan(GDC_TEXT_STAT, &probe_raw[1]);', '    (void)probe_raw[1];'),
)
def ordinary():
    with tempfile.TemporaryDirectory(prefix='splash-probe-ordinary-') as td:
        obj = Path(td) / 'probe.o'
        cross = Path(os.environ['CROSS_DIR']) / 'bin'
        subprocess.run([str(cross / 'i386-elf-gcc'), '-std=gnu11', '-O2',
            '-ffreestanding', *include_flags(ROOT),
            '-c', str(ROOT / SOURCE), '-o', str(obj)], check=True)
        assert not subprocess.check_output([str(cross / 'i386-elf-nm'), str(obj)]).strip()
    print('GREEN ordinary build contains no probe code/storage', flush=True)
    with tempfile.TemporaryDirectory(prefix='splash-probe-instructions-') as td:
        obj = Path(td) / 'probe.o'
        subprocess.run([str(cross / 'i386-elf-gcc'), '-std=gnu11', '-O2',
            '-march=i386', '-ffreestanding', '-D__KERNEL_BUILD__', '-DSPLASH_RASTER_DIAG',
            *include_flags(ROOT), '-I'+str(ROOT/'drivers/video/pc98'),
            '-c', str(ROOT / SOURCE), '-o', str(obj)], check=True)
        asm = subprocess.check_output([str(cross / 'i386-elf-objdump'), '-d',
            '--disassemble=probe_pl_success', str(obj)], text=True)
        jumps = re.findall(r'\t(j[a-z]+)\s', asm)
        assert len([j for j in jumps if j != 'jmp']) >= 4, asm
        assert re.search(r'\tin\s', asm) and re.search(r'\tcall\s+\*', asm), asm
    print('GREEN product joint machine code retains IN, guards, indirect emit', flush=True)
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
            if rel == SOURCE:
                source = source.replace('    int result = -1;\n',
                    '    int result = -1;\n    probe_host_success_enter();\n')
                source = source.replace('    for (u32 i = 1; ; i++) {',
                    '    success_scanning = 1;\n    for (u32 i = 1; ; i++) {')
                source = source.replace('    if (stamp->clocks & PROBE_PL_STAMP_END)',
                    '    success_scanning = 0;\n    if (stamp->clocks & PROBE_PL_STAMP_END)')
                source = source.replace('    stamp->iterations = *iterations;\n    return result;',
                    '    stamp->iterations = *iterations;\n    probe_host_success_return();\n    return result;')
                source = source.replace(
                    'unsigned long long span = probe_pl_batch(kind, c, emit, count, phase);\n        probe_measure_restore(flags, s);',
                    'unsigned long long span = probe_pl_batch(kind, c, emit, count, phase);\n        probe_measure_restore(flags, s);\n        span = probe_host_pl_span(span);')
                source = source.replace('    unsigned long long seed_closed =',
                    '    seed = probe_host_error_span(seed, 1);\n    unsigned long long seed_closed =')
                source = source.replace('        unsigned long long closed =',
                    '        span = probe_host_error_span(span, 0);\n        unsigned long long closed =')
                source = source.replace('probe_hcalibrate(s, &q, clocks, pass)',
                    'probe_host_hcalibrate(s, &q, clocks, pass)')
                source = source.replace('static u32 probe_hcollect(u32 count, u32 completed)\n{',
                    'static u32 probe_hcollect(u32 count, u32 completed)\n{\n    probe_hcollect_check();')
            dest.write_text(source)
        cc = str(Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc'); exe = work/'probe'
        # Eight frames retain the nine edges used by classification; 32 H periods
        # retain the calibrated cap boundary. 200000 polls cover fake frame waits,
        # 64 clock samples suffice, and 4096 H samples cover the slowest fixture.
        subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
            '-D__KERNEL_BUILD__', '-DSPLASH_RASTER_DIAG',
            '-DPROBE_FRAMES=8u', '-DPROBE_HPERIODS=32u', '-DPROBE_POLLS=200000u',
            '-DPROBE_SAMPLES=64u', '-DPROBE_HSAMPLES=4096u', *include_flags(ROOT),
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
    print('GREEN boot BIOS snapshot before/after begin, four wave types, independent IN/clock/OUT costs, cumulative IRQ/J_max, signed margins, buffered output, bounded failure, certified N5/N6 normal H windows, 38H IF0 cap, calibrated B error, per-column/phase/kind batch maximum frequency and distinct second, raw/corrected Q parts, physical low/high N0..6 body and disjoint outside spans, successful return/prep/next-wait linkage, ordered B periods', flush=True)
    if args.mutate:
        for mutation in MUTATIONS:
            rc = execute(mutation)
            assert rc != 0, mutation[0]
            print(f'RED {mutation[0]}: compiled, runtime rc={rc}', flush=True)
def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_splash_probe.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
if __name__ == '__main__': main()
