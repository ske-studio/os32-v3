"""TSC integer conversion, fake endpoints, calibration and permanent fallback."""
CHECK = dict(name='tick-tsc-host', tier=4, needs=[], budget=5.0,
    covers=['platform/pc98/tsc_clock.[ch]', 'platform/pc98/tsc_math.c',
            'platform/pc98/pit.h', 'platform/pc98/pit_math.h',
            'arch/x86/arch_cpu.h', 'kernel/idle_work.h', 'kernel/irq/irq.h',
            'kernel/time/tick_clock.h', 'include/types.h', 'lib/divdi3.c',
            'tools/tests/tick_tsc_host.c', 'tools/tests/test_tick_tsc.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'])
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from host32 import run as host_run
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('platform/pc98/tsc_math.c', '(delta >> 32) * mult', '0ULL * mult'),
    ('platform/pc98/tsc_clock.c', 'epoch_fraction = fraction;', 'epoch_fraction = 0;'),
    ('platform/pc98/tsc_clock.c', 'if (!arch_cpu_has_tsc())', 'if (0)'),
    ('platform/pc98/tsc_clock.c', 'if (now < origin)', 'if (0)'),
    ('platform/pc98/tsc_clock.c', 'k >= 1 && k <= 2', 'k >= 1'),
    ('platform/pc98/tsc_clock.c', 'distance <= limit', 'distance <= (limit & 0u)'),
    ('platform/pc98/tsc_clock.c', 'if (++diag.missing == 1)', 'if (1)'),
    ('platform/pc98/tsc_clock.c', 'diag.abnormal = 0;\n    if (reason', '(void)0;\n    if (reason'),
    ('platform/pc98/tsc_clock.c', 't > w->us && missing_window(d, w->us)', 'missing_window(d, w->us)'),
    ('platform/pc98/tsc_clock.c', 'w->cost <= allowance', 'w->cost <= allowance * 2'),
    ('platform/pc98/tsc_clock.c', 'high - low > low / 2000u', 'high - low > low'),
    ('platform/pc98/tsc_clock.c', 'elapsed > TSC_WINDOW_MAX_US', 'elapsed > TSC_BOOT_LIMIT_US'),
    ('platform/pc98/tsc_clock.c', 'if (!_irq_enabled() || kctx_irq_depth)', 'if (0)'),
)


def verify(work, mutation=None):
    for rel in ('platform/pc98/tsc_clock.c', 'platform/pc98/tsc_math.c',
                'platform/pc98/tsc_clock.h'):
        source = (ROOT / rel).read_text()
        if mutation and rel == mutation[0]:
            assert source.count(mutation[1]) == 1, mutation
            source = source.replace(mutation[1], mutation[2])
        if rel.endswith('tsc_clock.c'):
            # Only the CPU instruction boundary changes; all policy is product C.
            source = source.replace('__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) : : "memory");',
                                    'tsc_fake(&lo, &hi);')
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(source)
    # Model instruction results while executing the actual capability guards.
    cpu = (ROOT / 'arch/x86/arch_cpu.h').read_text()
    cpu = cpu[cpu.index('static inline int arch_cpu_has_cpuid'):cpu.index('#endif /* ARCH_CPU_H */')]
    cpu = cpu.replace('__asm__ volatile("pushfl; popl %0" : "=r"(old));', 'old = fake_flags;')
    cpu, n = re.subn(r'__asm__ volatile\("pushl %0; popfl; pushfl; popl %1; pushl %2; popfl".*?;',
                    'changed = mutable_id ? probe : old;', cpu, flags=re.S)
    assert n == 1
    cpu, n = re.subn(r'__asm__ volatile\("cpuid".*?;',
                    '++cpuid_calls; d = a ? features : 0; a = a ? 0 : max_leaf; b = c = 0; (void)b; (void)c; (void)d;', cpu)
    assert n == 3
    (work / 'cpu.h').write_text(cpu)
    (work / 'io.h').write_text('unsigned int irq_save(void);\nvoid irq_restore(unsigned int);\nint _irq_enabled(void);\n')
    exe = work / 'test'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                    '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    '-nostdlib', '-static', '-no-pie', '-D__cdecl=', '-I' + str(work),
                    *include_flags(ROOT), str(ROOT / 'tools/tests/tick_tsc_host.c'),
                    str(ROOT / 'lib/divdi3.c'), '-o', str(exe)], check=True)
    return host_run([str(exe)], cwd=work, timeout=5).returncode


def main(mutate=False):
    with tempfile.TemporaryDirectory(prefix='tick-tsc-') as td:
        work = Path(td)
        assert verify(work) == 0
        print('PASS TSC math / CPU capability / calibration / monitor', flush=True)
        if mutate:
            for mutation in MUTATIONS:
                rc = verify(work, mutation)
                assert rc == 1, (mutation, rc)
                print('RED runtime', mutation[1], 'rc=1', flush=True)
    return 0


def run(ctx):
    return main(ctx.mutate)


if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
