"""Segment and staging acceptance on the freestanding 32-bit ABI."""
from pathlib import Path
import subprocess
import tempfile

from host32 import run as host_run
from kernel_includes import include_flags

CHECK = dict(name='snd2-seg-host', tier=4, needs=[], budget=3,
             covers=['sound/hal/snd2_seg.c', 'sound/hal/snd2_stg.c',
                     'sound/hal/snd2_int.h', 'sound/hal/snd2_ring_math.h',
                     'include/av_contract.h', 'lib/kstring.h', 'mm/kmalloc.h', 'include/types.h',
                     'sdk/include/os32/*.h', 'tools/tests/snd2_seg_host.c',
                     'tools/tests/test_snd2_seg.py', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'build/config.mk'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §3-4・§3-5・§13-2'])

ROOT = Path(__file__).resolve().parents[2]
HAL = ROOT / 'sound/hal'
MUTATIONS = [
    ('seg', 's->gap_next = s->seg_n[next] ? s->seg_first[next] : ~0U;',
     's->gap_next = next;'),
    ('seg', 'last_obs_ms >= revolution_ms', 'last_obs_ms > revolution_ms'),
    ('seg', 'last_progress_ms >= revolution_ms', 'last_progress_ms > revolution_ms'),
    ('seg', 'if (!n) return;', 'if (!n || used) return;'),
    ('seg', 's->played += n;', 's->played += s->period;'),
    ('seg', 'n < s->period)', 'n < s->period && s->played == s->copied)'),
    ('seg', 's->copied = s->played;\n    s->last_hw', 's->last_hw'),
    ('seg', 's->period - s->margin', 's->period'),
    ('seg', 'i <= s->lead; ++i)\n', 'i < s->lead; ++i)\n'),
    ('seg', '? 2 : 1', '? 1 : 1'),
    ('seg', 'if (!s->paused) snd2_seg_prime(s);', 'snd2_seg_prime(s);'),
    ('seg', '++s->epoch;', '/* epoch lost */'),
    ('stg', 's->written - s->played', 's->written - s->copied'),
    ('stg', 'kmemcpy(s->stg, (const', 'kmemcpy(s->stg + 1, (const'),
]


def verify(work, mutation=None):
    sources = []
    for unit in ('seg', 'stg'):
        path = work / f'snd2_{unit}.c'
        source = (HAL / path.name).read_text()
        if mutation and mutation[0] == unit:
            _, old, new = mutation
            assert source.count(old) == 1, mutation
            source = source.replace(old, new)
        path.write_text(source)
        sources.append(path)
    exe = work / 'test'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-O2', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    '-nostdlib', '-static', '-no-pie', '-D__cdecl=', '-I' + str(HAL),
                    *include_flags(ROOT), *sources, ROOT / 'tools/tests/snd2_seg_host.c',
                    '-o', exe], check=True)
    return host_run([str(exe)], cwd=work, timeout=3).returncode


def run(ctx):
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert verify(work) == 0
        if ctx.mutate:
            for mutation in MUTATIONS:
                assert verify(work, mutation) == 1, mutation
                print('RED runtime', mutation[1])
    return 0
