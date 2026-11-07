"""Real sys_time_now/provider contracts, including protected user outputs."""
CHECK = dict(name='clock-caller-host', tier=4,
    covers=['lib/divdi3.c', 'tools/tests/test_clock_caller.py', 'tools/tests/clock_caller_host.c',
            'kernel/time/ktime.c', 'kernel/time/ktime.h', 'kernel/time/time_math.c',
            'kernel/time/time_math.h', 'platform/pc98/clock_source.c',
            'platform/pc98/pit_time.c', 'platform/pc98/pit_time.h',
            'platform/pc98/pit.h', 'platform/pc98/pit_math.h',
            'include/clock_source.h', 'include/kernel_tick.h', 'include/platform_timer.h',
            'tools/tests/dep_dir_b_io_shim.h', 'tools/tests/kernel_includes.py', 'tools/tests/host32.py'],
    needs=[], budget=3)
from pathlib import Path
import subprocess
import tempfile
import sys
from kernel_includes import include_flags
from host32 import run as host_run
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('platform/pc98/clock_source.c', 'attempt < TIME_RETRY_MAX;', 'attempt < 1;'),
    ('kernel/time/ktime.c', 'if (time_clamp(us, s_last_us, &us)) ktime_clamp_count++;',
     '(void)time_clamp;'),
    ('kernel/time/ktime.c', '    irq_restore(saved);',
     '    if (rc == 0 && lo) *lo = out_lo;\n    irq_restore(saved);'),
)


def verify(work, mutation=None):
    for rel in ('platform/pc98/clock_source.c', 'kernel/time/ktime.c'):
        text = (ROOT / rel).read_text()
        if mutation and rel == mutation[0]:
            assert text.count(mutation[1]) == 1
            text = text.replace(mutation[1], mutation[2])
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text)
    harness = work / 'caller.c'
    harness.write_text((ROOT / 'tools/tests/clock_caller_host.c').read_text().replace('"../../', '"'))
    (work / 'io.h').write_text((ROOT / 'tools/tests/dep_dir_b_io_shim.h').read_text())
    exe = work / 'caller'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                    '-static', '-no-pie', '-D__cdecl=', '-I' + str(work), *include_flags(ROOT),
                    str(harness), str(ROOT / 'platform/pc98/pit_time.c'),
                    str(ROOT / 'kernel/time/time_math.c'), str(ROOT / 'lib/divdi3.c'),
                    '-o', str(exe)], check=True)
    return host_run([str(exe)], cwd=work).returncode


def main(mutate=False):
    with tempfile.TemporaryDirectory(prefix='clock-caller-') as td:
        work = Path(td)
        assert verify(work) == 0
        print('PASS real clock caller: NOSYS/AGAIN unwritten, IF=0/1, both frequencies, retry, clamp, protected outputs', flush=True)
        if mutate:
            for mutation in MUTATIONS:
                rc = verify(work, mutation)
                assert rc != 0, mutation
                print('RED runtime', mutation[1], 'rc=', rc, flush=True)
    return 0


def run(ctx):
    return main(ctx.mutate)


if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
