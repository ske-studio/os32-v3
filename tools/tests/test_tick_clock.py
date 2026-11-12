"""Exercise the common physical-to-logical clock without libc."""
CHECK = dict(name='tick-clock-host', tier=4,
    covers=['kernel/time/tick.c', 'kernel/time/tick_clock.h',
            'include/kernel_tick.h', 'sdk/include/os32/os32_kapi_shared.h',
            'tools/tests/tick_clock_host.c', 'tools/tests/test_tick_clock.py',
            'tools/tests/kernel_includes.py', 'tools/tests/host32.py'],
    needs=[], budget=3.0)
from pathlib import Path
import subprocess
import sys
import tempfile
from host32 import run as host_run
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('while (ms_phase >= clk)', 'if (ms_phase >= clk)'),
    ('if (!clk) {', 'if (0) {'),
    ('if (num * OS32_TICK_HZ > den)', 'if (0)'),
    ('if (!++raw_lo) ++raw_hi;', '++raw_lo;'),
)


def verify(work, mutation=None):
    source = (ROOT / 'kernel/time/tick.c').read_text()
    if mutation:
        assert source.count(mutation[0]) == 1
        source = source.replace(*mutation)
    dest = work / 'kernel/time'
    dest.mkdir(parents=True, exist_ok=True)
    (dest / 'tick.c').write_text(source)
    (dest / 'tick_clock.h').write_text((ROOT / 'kernel/time/tick_clock.h').read_text())
    exe = work / 'test'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    '-nostdlib', '-static', '-no-pie', '-D__cdecl=', '-I' + str(work),
                    *include_flags(ROOT), str(ROOT / 'tools/tests/tick_clock_host.c'),
                    '-o', str(exe)], check=True)
    try:
        return host_run([str(exe)], cwd=work, timeout=3).returncode
    except subprocess.TimeoutExpired:
        return 124


def main(mutate=False):
    with tempfile.TemporaryDirectory(prefix='tick-clock-') as td:
        work = Path(td)
        assert verify(work) == 0
        print('PASS tick clock', flush=True)
        if mutate:
            for mutation in MUTATIONS:
                rc = verify(work, mutation)
                assert rc != 0, mutation
                print('RED runtime', mutation[0], 'rc=', rc, flush=True)
    return 0


def run(ctx):
    return main(ctx.mutate)


if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
