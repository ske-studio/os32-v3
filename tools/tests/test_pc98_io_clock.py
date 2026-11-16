#!/usr/bin/env python3
"""IO_TIER T1/P5/P7: actual PIT/TSC provider, freestanding ILP32."""
CHECK = dict(name='pc98-io-clock-host', tier=3, budget=15, needs=[], covers=[
    'platform/pc98/clock_source.[ch]', 'platform/pc98/tsc_clock.[ch]',
    'platform/pc98/tsc_math.c', 'platform/pc98/pit_time.[ch]',
    'platform/pc98/pit.h', 'platform/pc98/pit_math.h', 'include/pc98.h',
    'include/clock_source.h', 'include/platform_timer.h', 'include/kernel_tick.h',
    'kernel/io_clock.h', 'kernel/time/ktime.h', 'kernel/time/tick_clock.h',
    'kernel/irq/tick_budget.h', 'kernel/irq/irq.h', 'kernel/io_work.h', 'kernel/io_policy.h',
    'include/types.h', 'include/callconv.h', 'lib/divdi3.c',
    'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_generations.h',
    'tools/tests/pc98_io_clock_host.c', 'tools/tests/test_pc98_io_clock.py',
    'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    basis=['IO_TIER_DESIGN §5', 'P5', 'P7'])
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

MUTANTS = [
    ('mul rounds up', 'platform/pc98/clock_source.c',
     'n / denominator; /* IO_MUL_FLOOR */',
     '(n + denominator - 1) / denominator; /* IO_MUL_FLOOR */'),
    ('fixed shift', 'platform/pc98/clock_source.c',
     'io_mul = (u32)m;', 'io_mul = (u32)m; io_shift = 24;'),
    ('ignores token generation', 'platform/pc98/clock_source.c',
     'source != io_source || generation != io_generation', 'source != io_source || (generation & 0u)'),
    ('no half-wrap guard', 'platform/pc98/clock_source.c',
     'value >= 0x80000000ULL', '0'),
    ('no source generation', 'platform/pc98/clock_source.c',
     '++io_generation; /* IO_SOURCE_GENERATION */', '(void)io_generation; /* IO_SOURCE_GENERATION */'),
    ('no calibration generation', 'platform/pc98/tsc_clock.c',
     '++tsc_generation; /* IO_TSC_READY_GENERATION */', '(void)tsc_generation; /* IO_TSC_READY_GENERATION */'),
]
PRODUCTS = ('platform/pc98/clock_source.c', 'platform/pc98/tsc_clock.c')
HEADERS = ('platform/pc98/clock_source.h', 'platform/pc98/tsc_clock.h',
           'include/clock_source.h', 'kernel/io_clock.h')


def exercise(root, work, mutation=None):
    # Copies include the local forwarding header; -I root supplies unchanged
    # kernel/platform dependencies identically to control and every mutant.
    for rel in (*PRODUCTS, *HEADERS):
        source = (root / rel).read_text()
        if mutation and rel == mutation[1]:
            _, _, old, new = mutation
            assert source.count(old) == 1, mutation[0]
            source = source.replace(old, new, 1)
        if rel == PRODUCTS[0]:
            source = source.replace('__asm__ volatile("rdtsc" : "=a"(value), "=d"(high) : : "memory");',
                                    'tsc_fake(&value, &high);')
        if rel == PRODUCTS[1]:
            source = source.replace('__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) : : "memory");',
                                    'tsc_fake(&lo, &hi);')
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(source)
    (work / 'io.h').write_text('unsigned int irq_save(void);\nvoid irq_restore(unsigned int);\nint _irq_enabled(void);\n')
    (work / 'cpu.h').write_text('static int arch_cpu_has_tsc(void) { return 1; }\nstatic void arch_cpu_serialize(void) {}\n')
    exe = work / 'host'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
        '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
        '-I' + str(work), *include_flags(root),
        str(root / 'tools/tests/pc98_io_clock_host.c'),
        str(root / 'platform/pc98/tsc_math.c'), str(root / 'platform/pc98/pit_time.c'),
        str(root / 'lib/divdi3.c'), '-o', str(exe)], check=True)
    result = host32.run([str(exe)], capture_output=True)
    if result.returncode and result.stdout:
        print('failed C assertion line:', int.from_bytes(result.stdout[:4], 'little'), flush=True)
    return result.returncode


def run(ctx):
    with tempfile.TemporaryDirectory(prefix='pc98-io-clock-') as td:
        work = Path(td)
        assert exercise(ctx.root, work) == 0, 'normal control'
        print('PASS PC98 PIT/TSC provider, rate/token/IF/wrap/fallback', flush=True)
        if ctx.mutate:
            for mutation in MUTANTS:
                assert exercise(ctx.root, work, mutation) == 1, mutation[0]
                print('RED runtime: ' + mutation[0], flush=True)
    return 0


if __name__ == '__main__':
    from types import SimpleNamespace
    sys.exit(run(SimpleNamespace(root=Path(__file__).resolve().parents[2],
        mutate=os.environ.get('MUTATE') == '1' or '--mutate' in sys.argv)))
