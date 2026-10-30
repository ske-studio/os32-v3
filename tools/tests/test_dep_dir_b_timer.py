"""L13 fixed trace, captured once from 12b7e2fe with this harness and shim.

Capture procedure: pass baseline=(sysclk.c, idt.c, ktime.c) text to build(),
trace each TEST_CLOCK_BYTE. idt.c is sliced at its PIT state, without edits.
The sources are also identical at the L13 step base 5399711c. Input read
sequences are explicitly defined by samples() in dep_dir_b_timer_host.c.
Runtime only links the current tree and compares the checked-in fixture.
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
SOURCES = ('kernel/time/sysclk.c', 'kernel/time/pit.c', 'kernel/time/ktime.c',
           'kernel/time/pit_math.c', 'kernel/time/time_math.c',
           'platform/pc98/clock.c', 'platform/pc98/timer.c', 'platform/pc98/pic.c')
HEADERS = ('include/platform_timer.h', 'include/platform_irq.h', 'arch/x86/kernel/idt.h',
           'arch/x86/kernel/gdt.h', 'kernel/irq/irq_math.h', 'kernel/time/pit_math.h', 'kernel/time/time_math.h',
           'kernel/time/ktime.h', 'kernel/time/sysclk.h', 'kernel/v86/v86_bios.h')
MUTATIONS = (
    ('platform/pc98/clock.c',
     'out->is_8mhz ? (unsigned long)SYSCLK_1997 : (unsigned long)SYSCLK_2458',
     'out->is_8mhz ? (unsigned long)SYSCLK_2458 : (unsigned long)SYSCLK_1997'),
    ('platform/pc98/timer.c',
     '    outp(PIT_CNTR0, reload & 0xFF);\n    outp(PIT_CNTR0, (reload >> 8) & 0xFF);',
     '    outp(PIT_CNTR0, (reload >> 8) & 0xFF);\n    outp(PIT_CNTR0, reload & 0xFF);'),
    ('platform/pc98/timer.c',
     '    out->p1 = platform_irq_timer_pending();',
     '    out->p1 = platform_irq_timer_pending();\n    out->p2 = platform_irq_timer_pending();'),
    ('kernel/time/ktime.c', 'attempt < TIME_RETRY_MAX;', 'attempt <= TIME_RETRY_MAX;'),
    ('kernel/time/ktime.c', '        platform_timer_sample(&hw);',
     '        t = (unsigned int)tick_count;\n        platform_timer_sample(&hw);'),
    ('kernel/time/ktime.c', 'if (lo) *lo = (u32)out_lo;',
     'if (lo) *lo = (u32)out_lo ^ (attempt == 1);'),
    ('kernel/time/ktime.c', 'if (hi) *hi = (u32)out_hi;',
     'if (hi) *hi = (u32)out_hi ^ (attempt == 1);'),
)
# Relocations remove the old operation, rather than adding another sample.
REMOVALS = {2: '    out->p2 = platform_irq_timer_pending();',
            4: '        t = (unsigned int)tick_count;'}


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
        clock = (work / 'kernel/time/sysclk.c').read_text()
        pit = (work / 'kernel/time/pit.c').read_text()
        ktime = (work / 'kernel/time/ktime.c').read_text()
    else:
        clock, idt, ktime = baseline
        pit = re.search(r'static struct pit_setup s_pit.*?^int pit_init\(unsigned int hz\)\n\{.*?^\}', idt, re.M | re.S)[0]
    for name, text in (('clock', clock), ('pit', pit), ('ktime', ktime)):
        (work / (name + '_source.inc')).write_text(text)
    exe = work / 'timer-test'
    linked = ['kernel/time/pit_math.c', 'kernel/time/time_math.c']
    if baseline is None:
        linked += ['platform/pc98/clock.c', 'platform/pc98/timer.c', 'platform/pc98/pic.c']
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
        assert actual == expected['traces'][str(byte)], (byte, actual)
        with tempfile.TemporaryDirectory(prefix='dep-dir-b-retry-') as td:
            trace(build(Path(td), byte, mutation, retry_value=True))
    print('PASS L13 clock/default/once, PIT/fallback/state, sample7/tick/feed, retry/clamp/IF/output', flush=True)
    print('PASS retry -> success value: both clocks, IF=0/1, lo/hi after u32 rollover', flush=True)


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
