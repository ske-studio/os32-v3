"""L13 fixed trace, captured once from 12b7e2fe with this harness and shim.

Capture procedure: pass baseline=(sysclk.c, idt.c, ktime.c) text to build(),
trace each TEST_CLOCK_BYTE. idt.c is sliced at its PIT state, without edits.
The sources are also identical at the L13 step base 5399711c. Input read
sequences are explicitly defined by samples() in dep_dir_b_timer_host.c.
Runtime only links the current tree and compares the checked-in fixture.
E08 stage 2 removes only the two feed-only calls per clock; every remaining
I/O, return/output and IF row retains its original expected value.
"""
from kernel_includes import include_flags
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile

from test_dep_dir_b_io import trace

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tools/tests/dep_dir_b_l13_baseline.json'
SOURCES = ('sys/platform/pc98/sysclk.c', 'sys/platform/pc98/pit.c', 'sys/kern/time/ktime.c', 'sys/platform/pc98/clock_source.c', 'sys/platform/pc98/pit_time.c',
           'sys/platform/pc98/pit_math.c', 'sys/kern/time/time_math.c',
           'sys/platform/pc98/clock.c', 'sys/platform/pc98/timer.c', 'sys/platform/pc98/pic.c')
HEADERS = ('sys/platform/pc98/tsc_clock.h', 'sys/platform/pc98/clock_source.h', 'sys/kern/io_clock.h', 'sys/kern/irq/tick_budget.h', 'sys/include/platform_timer.h', 'sys/hal/time/clock_source.h', 'sys/platform/pc98/pit_time.h', 'sys/include/platform_irq.h', 'sys/platform/pc98/platform_irq_limits.h', 'sys/arch/x86/kernel/idt.h', 'sys/platform/pc98/pit.h', 'sys/include/kernel_tick.h',
           'sys/arch/x86/kernel/gdt.h', 'sys/kern/irq/irq_math.h', 'sys/platform/pc98/pit_math.h', 'sys/kern/time/time_math.h',
           'sys/kern/time/ktime.h', 'sys/hal/time/tick_clock.h',
           'sys/platform/pc98/sysclk.h', 'sys/platform/pc98/legacy/v86_bios.h', 'sys/platform/pc98/bios_snapshot.h', 'sys/platform/pc98/platform_pic.h', 'sys/include/irq_line.h')
MUTATIONS = (
    ('sys/platform/pc98/clock.c',
     'out->is_8mhz ? (unsigned long)SYSCLK_1997 : (unsigned long)SYSCLK_2458',
     'out->is_8mhz ? (unsigned long)SYSCLK_2458 : (unsigned long)SYSCLK_1997'),
    ('sys/platform/pc98/timer.c',
     '    outp(PIT_CNTR0, reload & 0xFF);\n    outp(PIT_CNTR0, (reload >> 8) & 0xFF);',
     '    outp(PIT_CNTR0, (reload >> 8) & 0xFF);\n    outp(PIT_CNTR0, reload & 0xFF);'),
    ('sys/platform/pc98/timer.c',
     '    out->p1 = platform_irq_timer_pending();',
     '    out->p1 = platform_irq_timer_pending();\n    out->p2 = platform_irq_timer_pending();'),
    ('sys/platform/pc98/clock_source.c', 'attempt < TIME_RETRY_MAX;', 'attempt <= TIME_RETRY_MAX;'),
    ('sys/platform/pc98/clock_source.c', '        platform_timer_sample(&hw);',
     '        tick_clock_raw(&raw_lo, &raw_hi);\n        platform_timer_sample(&hw);'),
    ('sys/platform/pc98/clock_source.c', '*lo = out_lo;', '*lo = out_lo ^ (attempt == 1);'),
    ('sys/platform/pc98/clock_source.c', '*hi = out_hi;', '*hi = out_hi ^ (attempt == 1);'),
    ('sys/kern/time/ktime.c', 'if (time_clamp(us, s_last_us, &us)) ktime_clamp_count++;',
     '(void)s_last_us;'),
    ('sys/kern/time/ktime.c', 'if (time_clamp(us, s_last_us, &us)) ktime_clamp_count++;',
     '(void)time_clamp(us, s_last_us, &us);'),
)
# Relocations remove the old operation, rather than adding another sample.
REMOVALS = {2: '    out->p2 = platform_irq_timer_pending();',
            4: '        tick_clock_raw(&raw_lo, &raw_hi);'}


def build(work, clock_byte, mutation=None, baseline=None, retry_value=False):
    # B5: positive and mutation copies use the SAME tree, headers and paths.
    for rel in (*SOURCES, *HEADERS):
        text = (ROOT / rel).read_text()
        if mutation and mutation[0] == rel:
            assert text.count(mutation[1]) == 1
            index = MUTATIONS.index(mutation)
            if index in REMOVALS:
                assert text.count(REMOVALS[index]) == 1
                text = text.replace(REMOVALS[index], '', 1)
            text = text.replace(mutation[1], mutation[2], 1)
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text)
    (work / 'io.h').write_text((ROOT / 'tools/tests/dep_dir_b_io_shim.h').read_text())
    if baseline is None:
        clock = (work / 'sys/platform/pc98/sysclk.c').read_text()
        pit = (work / 'sys/platform/pc98/pit.c').read_text()
        ktime = (work / 'sys/platform/pc98/clock_source.c').read_text() + '\n' + (work / 'sys/kern/time/ktime.c').read_text()
    else:
        clock, idt, ktime = baseline
        pit = re.search(r'static struct pit_setup s_pit.*?^int pit_init\(unsigned int hz\)\n\{.*?^\}', idt, re.M | re.S)[0]
    for name, text in (('clock', clock), ('pit', pit), ('ktime', ktime)):
        (work / (name + '_source.inc')).write_text(text)
    exe = work / 'timer-test'
    linked = ['sys/platform/pc98/pit_math.c', 'sys/kern/time/time_math.c', 'sys/platform/pc98/pit_time.c']
    if baseline is None:
        linked += ['sys/platform/pc98/clock.c', 'sys/platform/pc98/timer.c', 'sys/platform/pc98/pic.c']
    libgcc = subprocess.check_output([str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'), '-print-libgcc-file-name'], text=True).strip()
    subprocess.run([
        'gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
        '-static', '-no-pie', '-ffunction-sections', '-Wl,--gc-sections',
        '-D__cdecl=', f'-DTEST_CLOCK_BYTE={clock_byte}',
        *(['-DTEST_RETRY_VALUE'] if retry_value else []),
        '-I' + str(work), *include_flags(work),
        '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'sdk/include/os32'),
        str(ROOT / 'tools/tests/dep_dir_b_timer_host.c'),
        *(str(work / rel) for rel in linked), libgcc, '-o', str(exe)], check=True)
    return exe


def verify(mutation=None):
    expected = json.loads(FIXTURE.read_text())
    for byte in (0, 128):
        with tempfile.TemporaryDirectory(prefix='dep-dir-b-timer-') as td:
            actual = trace(build(Path(td), byte, mutation))
        baseline = list(expected['traces'][str(byte)])
        # D: invalid setup is checked inside the saved-IF provider contract.
        # Only these three NOSYS calls gain a balanced save/restore pair.
        # 6: IO_TIER P6 pc98_io_clock_install after a successful init.
        for at in (11, 10, 6, 0):
            baseline[at:at] = [['S', 0, 0, 0], ['F', 0, 0, 0]]
        assert actual == baseline, (byte, actual)
        with tempfile.TemporaryDirectory(prefix='dep-dir-b-retry-') as td:
            trace(build(Path(td), byte, mutation, retry_value=True))
    print('PASS L13 clock/default/once, PIT/fallback/state, sample7/tick, retry/clamp/IF/output', flush=True)
    print('PASS retry -> success / equal / clamp: both clocks, IF=0/1, lo/hi after u32 rollover', flush=True)


if __name__ == '__main__':
    verify()
    if '--mutate' in sys.argv:
        for mutation in MUTATIONS:
            try:
                verify(mutation)
            except AssertionError:
                print('RED', mutation[1], flush=True)
            else:
                raise AssertionError('mutation escaped: ' + mutation[1])
