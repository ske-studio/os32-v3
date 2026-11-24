"""PCM stream lifecycle acceptance on the freestanding 32-bit ABI."""
from pathlib import Path
import subprocess
import tempfile

from host32 import run as host_run
from kernel_includes import include_flags

CHECK = dict(name='snd2-stream-host', tier=4, needs=[], budget=3,
             covers=['tools/tests/snd2_v1_host.c', 'include/pcm_contract.h', 'sound/hal/snd2_v1.c', 'sound/hal/snd2.h', 'sound/hal/snd2_stream.c', 'sound/hal/snd2_caps.c',
                     'sound/hal/snd2_reg.c', 'kernel/time/tick_clock.h', 'include/kernel_tick.h',
                     'include/io.h', 'sound/hal/snd2_seg.c', 'sound/hal/snd2_stg.c',
                     'sound/hal/snd2_int.h', 'sound/hal/snd2_ring_math.h',
                     'include/av_contract.h', 'lib/kstring.h', 'mm/kmalloc.h', 'include/types.h',
                     'sdk/include/os32/*.h', 'tools/tests/snd2_stream_host.c',
                     'tools/tests/test_snd2_stream.py', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'build/config.mk'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §3-4・§3-5・§3-6・§13-2'])

ROOT = Path(__file__).resolve().parents[2]
HAL = ROOT / 'sound/hal'
MUTATIONS = [
    ('v1', '>= 4096', '>= 4095'),
    ('v1', 's->written - s->played))', 's->written - s->copied))'),
    ('v1', 'PCM_CNT_REP_SHIFT)', 'PCM_CNT_UND_SHIFT)'),
    ('v1', 'SND2_CLOSE_DRAIN', 'SND2_CLOSE_ABORT'),
    ('stream', '((ms + 9) / 10 + 3)', '(ms / 10 + 3)'),
    ('stream', 's->state == SND2_ST_DRAIN || !buf', '!buf'),
    ('stream', 'hw_release(d->ctx, leak)', 'hw_release(d->ctx, 0)'),
    ('stream', '++s->gen;', '/* stale generation */'),
    ('stream', 'snd2_seg_reset(s, tick_clock_ms());\n        if (resync)', 'if (resync)'),
    ('stream', 'if (d->be->hw_stop(d->ctx) < 0) {\n                    teardown',
     'snd2_seg_reset(s, now);\n                if (d->be->hw_stop(d->ctx) < 0) {\n                    teardown'),
    ('stream', 'snd2_seg_resync(s, now);', 's->paused = 0; snd2_seg_resync(s, now);'),
    ('stream', 'if (resync) s->restart_pending = 1;', 'if (resync) s->state = SND2_ST_IDLE;'),
    ('stream', 's->paused = 0;\n            s->state = SND2_ST_RUN;', 's->paused = 0;'),
    ('stream', 'snd2_seg_pause(&d->stream);\n            d->stream.state = SND2_ST_PAUSE;',
     'snd2_seg_pause(&d->stream);'),
    ('stream', 'rc = s->played == s->written ? 0 : OS32_ERR_IO;', 'rc = 0;'),
    ('stream', 'if (!d->writing) snd2_stg_free(s);', 'snd2_stg_free(s);'),
]


def verify(work, mutation=None):
    sources = []
    (work / 'io.h').write_text('unsigned irq_save(void); void irq_restore(unsigned);\n')
    for unit in ('v1', 'stream', 'reg', 'caps', 'seg', 'stg'):
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
                    '-nostdlib', '-static', '-no-pie', '-D__cdecl=', '-I' + str(work), '-I' + str(HAL),
                    *include_flags(ROOT), *sources, ROOT / 'tools/tests/snd2_stream_host.c',
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
