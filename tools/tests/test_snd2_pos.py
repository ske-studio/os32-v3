"""PCM position and wait acceptance on the freestanding 32-bit ABI."""
from pathlib import Path
import subprocess
import tempfile

from host32 import run as host_run
from kernel_includes import include_flags

CHECK = dict(name='snd2-pos-host', tier=4, needs=[], budget=3,
             covers=['include/exec_wait_contract.h', 'sound/hal/snd2_refill_diag.h', 'sound/hal/snd2.h', 'sound/hal/snd2_stream.c', 'sound/hal/snd2_pos.c',
                     'kernel/time/tick_clock.h', 'include/kernel_tick.h',
                     'include/io.h', 'sound/hal/snd2_seg.c', 'sound/hal/snd2_stg.c',
                     'sound/hal/snd2_int.h', 'sound/hal/snd2_ring_math.h',
                     'include/av_contract.h', 'lib/kstring.h', 'mm/kmalloc.h', 'include/types.h',
                     'sdk/include/os32/*.h', 'tools/tests/snd2_pos_host.c',
                     'tools/tests/test_snd2_pos.py', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'build/config.mk'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §3-4・§3-5・§13-2'])

ROOT = Path(__file__).resolve().parents[2]
HAL = ROOT / 'sound/hal'
MUTATIONS = [
    ('pos', 'ring3_wait_pending() ||', ''),
    ('pos', 'pos.state == SND2_ST_PAUSE ||', ''),
    ('pos', 'else if (park)', 'else if (park && 0)'),
    ('pos', '                    _idle();', '                    irq_restore(saved); _idle();'),
    ('pos', '(frame - pos.written) > 0', '(frame - pos.written) < 0'),
    ('pos', 'pos.played - frame', 'snd2_device(id & 15U)->stream.played - frame'),
    ('pos', 'pos.time_ms - begin >=', 'pos.time_ms - begin >'),
    ('pos', '.epoch = s->epoch', '.epoch = 0'),
    ('pos', '    irq_restore(flags);', '',
     'int rc = snapshot_locked(id, out);',
     'irq_restore(flags);\n    int rc = snapshot_locked(id, out);'),
]


def verify(work, mutation=None):
    sources = []
    (work / 'io.h').write_text('unsigned irq_save(void); void irq_restore(unsigned); void _idle(void);\n')
    for unit in ('pos', 'stream', 'seg', 'stg'):
        path = work / f'snd2_{unit}.c'
        source = (HAL / path.name).read_text()
        if mutation and mutation[0] == unit:
            for old, new in zip(mutation[1::2], mutation[2::2], strict=True):
                assert source.count(old) == 1, mutation
                source = source.replace(old, new)
        path.write_text(source)
        sources.append(path)
    exe = work / 'test'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-O2', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    '-ffunction-sections', '-Wl,--gc-sections', '-nostdlib', '-static', '-no-pie', '-D__cdecl=', '-I' + str(work), '-I' + str(HAL),
                    *include_flags(ROOT), *sources, ROOT / 'tools/tests/snd2_pos_host.c',
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
