"""V7: real collector plus the real calibration recorder with a fake clock.

The clock/nop-loop stubs supply elapsed ticks only; calibration decisions and
arithmetic execute the actual source. The collector is compiled as a whole TU.
No probe, printing, allocation or scheduler symbols are provided to the link.
"""
from kernel_includes import include_flags
import subprocess
import tempfile
from pathlib import Path

import host32
from ver_test_utils import ROOT, SUPPORT, arguments, function


CALIBRATION_FIXTURE = r'''
#include "support.h"
#include "cpu_calibrate.h"
#include "cpu_calibrate_math.h"
static u32 clock_ticks, reads, rounds_run, ticks_per_round;
static u32 read_ticks(void) {
    /* Supply the initial synchronization edge, then only nop_loop advances. */
    if (reads++ == 1) clock_ticks++;
    return clock_ticks;
}
#define tick_count read_ticks()
static void nop_loop(u32 n) {
    CHECK(n == CALIBRATE_LOOPS);
    rounds_run++;
    clock_ticks += ticks_per_round;
}
#include "calibration.inc"
#include "calibration_math.c"
static void calibrate(u32 step, u32 expected_rounds, u32 lpt, int fallback) {
    clock_ticks = reads = rounds_run = 0;
    ticks_per_round = step;
    cpu_calibrate();
    CHECK(rounds_run == expected_rounds && cpu_calib_rounds == expected_rounds);
    CHECK(cpu_calib_ticks == expected_rounds * step);
    CHECK(cpu_loops_per_tick() == lpt);
    CHECK(cpu_calibrate_used_fallback() == fallback);
}
void _start(void) {
    CHECK(cpu_calibrate_used_fallback() == 1 && cpu_loops_per_tick() == 0);
    calibrate(5, 1, CALIBRATE_LOOPS / 5, 0);
    calibrate(0, CALIBRATE_MAX_ROUNDS, CALIBRATE_FALLBACK_LPT, 1);
    /* A legitimate measurement may equal the fallback value exactly. */
    calibrate(20, 1, CALIBRATE_FALLBACK_LPT, 0);
    calibrate(201, 1, CALIBRATE_FALLBACK_LPT, 1); /* below minimum LPT */
    calibrate(200, 1, CALIBRATE_MIN_LPT, 0); /* boundary is measured */
    calibrate(5, 1, CALIBRATE_LOOPS / 5, 0); /* replaces earlier fallback */
    say("PASS calibration: initial/default/measured/equal-value/minimum/reset\n");
    die(0);
}
'''


def build_run(fixture, runner, collector=None, calibration=None):
    with tempfile.TemporaryDirectory(prefix='ver-collect-') as directory:
        tmp = Path(directory)
        (tmp / 'support.h').write_text(SUPPORT)
        (tmp / 'fixture.c').write_text(fixture)
        sources = [str(tmp / 'fixture.c')]
        if collector is not None:
            (tmp / 'collector.c').write_text(collector)
            sources.append(str(tmp / 'collector.c'))
        if calibration is not None:
            (tmp / 'calibration.inc').write_text(calibration)
            (tmp / 'calibration_math.c').write_text(
                (ROOT / 'kernel/time/cpu_calibrate_math.c').read_text())
        obj = tmp / 'kstring.o'
        subprocess.run(['nasm', '-f', 'elf32', str(ROOT / 'lib/kstring_asm.asm'),
                        '-o', str(obj)], check=True)
        cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
               '-Wextra', '-Werror', '-Wno-unused-function', '-ffreestanding',
               '-fno-builtin', '-fno-pie', '-fno-stack-protector', '-nostdlib',
               '-static', '-no-pie', '-I' + str(tmp)]
        cmd += include_flags(ROOT)
        host32.build(cmd + sources + [str(obj), '-o', str(tmp / 'host')], check=True)
        result = host32.run([str(tmp / 'host')], runner=runner,
                            capture_output=True, text=True, timeout=30)
        print(result.stdout, end='')
        return result


def main():
    args = arguments()
    collector = (ROOT / 'kernel/ver_collect.c').read_text()
    fixture = (ROOT / 'tools/tests/ver_collect_host.c').read_text()
    cpu = (ROOT / 'kernel/time/cpu_calibrate.c').read_text()
    # Keep actual saved-state initializers and all decision/recording code.
    state = cpu[cpu.index('static u32 s_loops_per_tick'):cpu.index('/* CALIBRATE_LOOPS')]
    calibration = state + ''.join(function(cpu, sig) for sig in
        ('void cpu_calibrate(', 'u32 cpu_loops_per_tick(', 'int cpu_calibrate_used_fallback('))
    assert build_run(fixture, args.runner, collector=collector).returncode == 0
    assert build_run(CALIBRATION_FIXTURE, args.runner, calibration=calibration).returncode == 0
    if args.mutate:
        old = ('s->ser_ready = port_get_status(&s->ser_mode, &s->ser_baud,\n'
               '                                     &s->ser_fifo) >= 0;')
        assert collector.count(old) == 1
        mutant = collector.replace(old, old + '\n    s->ser_ready = 1;')
        result = build_run(fixture, args.runner, collector=mutant)
        assert result.returncode == 1 and 'FAIL: s->ser_ready ==' in result.stdout
        print('RED serial ready always 1')
        old = 's_used_fallback = ticks == 0 || total / ticks < (u32)CALIBRATE_MIN_LPT;'
        assert calibration.count(old) == 1
        mutant = calibration.replace(old, 's_used_fallback = s_loops_per_tick == CALIBRATE_FALLBACK_LPT;')
        result = build_run(CALIBRATION_FIXTURE, args.runner, calibration=mutant)
        assert result.returncode == 1 and 'FAIL: cpu_calibrate_used_fallback() == fallback' in result.stdout
        print('RED fallback inferred from value')


if __name__ == '__main__':
    main()
