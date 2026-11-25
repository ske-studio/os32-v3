"""Exercise the common physical-to-logical clock without libc."""
CHECK = dict(name='tick-clock-host', tier=4,
    covers=['sys/hal/sound/snd_wake_contract.h', 'sys/hal/sound/snd_wake_tick.c', 'sys/hal/sound/snd_wake_tick.h',
            'sys/hal/time/tick.c', 'sys/hal/time/tick_clock.h',
            'sys/include/kernel_tick.h', 'sdk/include/os32/os32_kapi_shared.h',
            'tools/tests/tick_clock_host.c', 'tools/tests/test_tick_clock.py',
            'tools/tests/kernel_includes.py', 'tools/tests/host32.py'],
    needs=[], budget=3.0,
    basis=['TICK_ARCH_DESIGN §2', 'TICK_1KHZ_NEXT_DESIGN §3-5・§6'],
)
from pathlib import Path
import subprocess
import sys
import tempfile
from host32 import run as host_run
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('if (!result && pcm_fn) pcm_fn();', '(void)pcm_fn;'),
    ('(i32)(now - due_ms) >= 0', 'now >= due_ms'),
    ('(i32)(now - due_ms) >= 0', '(i32)(now - due_ms) > 0'),
    ('armed = 0;\n        wake_notify', 'wake_notify'),
    ('if (!after_us) {\n        armed = 0;', 'if (!after_us) {'),
    ('fn && fast_fn && fn != fast_fn', '0'),
    ('fast_fn(ms_now);', 'fast_fn(ms_now - 1);'),
    ('return notify_step(result);\n    return result;', 'return notify_step(result);\n    return TICK_STEP_JIFFY;'),
    ('while (ms_phase >= clk)', 'if (ms_phase >= clk)'),
    ('if (!clk) {', 'if (0) {'),
    ('if (num * OS32_TICK_HZ > den)', 'if (0)'),
    ('if (!++raw_lo) ++raw_hi;', '++raw_lo;'),
)


def verify(work, mutation=None):
    paths = ['sys/hal/time/tick.c', 'sys/hal/time/tick_clock.h',
             'sys/hal/sound/snd_wake_tick.c', 'sys/hal/sound/snd_wake_tick.h']
    sources = {path: (ROOT / path).read_text() for path in paths}
    if mutation:
        assert sum(source.count(mutation[0]) for source in sources.values()) == 1
    for path, source in sources.items():
        dest = work / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(source.replace(*mutation) if mutation else source)
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
