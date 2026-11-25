"""TSC descriptor must match the real IO policy and dispatcher."""
CHECK = dict(name='tsc-registration-host', tier=3, budget=5, needs=[], covers=[
    'platform/pc98/tsc_clock.[ch]', 'sys/platform/pc98/tsc_math.c',
    'kernel/io_work.[ch]', 'kernel/io_policy.[ch]', 'sys/kern/io_clock.h',
    'tools/tests/tsc_registration_host.c', 'tools/tests/test_tsc_registration.py',
    'tools/tests/idle_io_sources.py', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'lib/divdi3.c'], basis=['IO_TIER_DESIGN P9'])
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import host32
from idle_io_sources import idle_io_sources
from kernel_includes import include_flags


def exercise(root, work, mutant):
    source = (root / 'sys/platform/pc98/tsc_clock.c').read_text()
    old = '.budget_min_us = 1, .budget_max_us = 500, .max_unit_us = 120,'
    assert source.count(old) == 1
    if mutant:
        source = source.replace(old, old.replace('500', '501'), 1)
    target = work / 'sys/platform/pc98/tsc_clock.c'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(source)
    (work / 'io.h').write_text('unsigned int irq_save(void);\nvoid irq_restore(unsigned int);\nint _irq_enabled(void);\n')
    (work / 'cpu.h').write_text('static int arch_cpu_has_tsc(void) { return 1; }\nstatic void arch_cpu_serialize(void) {}\n')
    exe = work / 'host'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
        '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
        '-static', '-no-pie', '-I' + str(work), *include_flags(root),
        str(root / 'tools/tests/tsc_registration_host.c'), *idle_io_sources(root, work),
        str(root / 'sys/platform/pc98/tsc_math.c'), str(root / 'lib/divdi3.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def run(ctx):
    with tempfile.TemporaryDirectory(prefix='tsc-registration-') as td:
        work = Path(td)
        assert exercise(ctx.root, work, False) == 0
        print('PASS TSC descriptor registration against real policy', flush=True)
        if ctx.mutate:
            assert exercise(ctx.root, work, True) == 1
            print('RED runtime: TSC budget_max_us mismatch', flush=True)
    return 0


if __name__ == '__main__':
    from types import SimpleNamespace
    sys.exit(run(SimpleNamespace(root=Path(__file__).resolve().parents[2],
        mutate=os.environ.get('MUTATE') == '1' or '--mutate' in sys.argv)))
