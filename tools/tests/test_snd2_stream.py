"""PCM stream lifecycle acceptance on the freestanding 32-bit ABI."""
from pathlib import Path
import subprocess
import tempfile

from host32 import run as host_run
from kernel_includes import include_flags

CHECK = dict(name='snd2-stream-host', tier=4, needs=[], budget=3,
             covers=['sys/hal/sound/snd2_refill_diag.h', 'sys/kern/time/tick_probe.h', 'sys/kern/time/tick_probe_ids.h', 'tools/tests/snd2_v1_host.c', 'sys/include/pcm_contract.h', 'sys/hal/sound/snd2_v1.c', 'sys/kapi/kapi_snd2.c', 'sys/kern/exec/exec.h', 'sys/include/user_read_contract.h', 'sys/include/memmap.h', 'sys/hal/sound/snd2.h', 'sys/hal/sound/snd2_stream.c', 'sys/hal/sound/snd2_caps.c',
                     'sys/hal/sound/snd2_reg.c', 'sys/hal/time/tick_clock.h', 'sys/include/kernel_tick.h',
                     'sys/include/io.h', 'sys/hal/sound/snd2_seg.c', 'sys/hal/sound/snd2_stg.c',
                     'sys/hal/sound/snd2_int.h', 'sys/hal/sound/snd2_ring_math.h',
                     'sys/include/av_contract.h', 'sys/kern/kstring.h', 'sys/kern/mm/kmalloc.h', 'sys/include/types.h',
                     'sdk/include/os32/*.h', 'tools/tests/snd2_stream_host.c',
                     'tools/tests/test_snd2_stream.py', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'build/config.mk'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §3-4・§3-5・§3-6・§13-2・§13-3'])

ROOT = Path(__file__).resolve().parents[2]
HAL = ROOT / 'sound/hal'
MUTATIONS = [
    ('stream', 'snd2_seg_after_publish(s, hw, tick_clock_ms());', '(void)hw;'),
    ('seg', 'refill(s, p, hw, 1);',
     'u32 before = s->copied; refill(s, p, hw, 1); s->copied = before;'),
    ('stream', 'return leak ? OS32_ERR_IO : 0;', 'return 0;'),
    ('stream', 'rc = s->state == SND2_ST_FAULTED || s->owner', 'rc = s->owner'),
    ('v1', '>= 4096', '>= 4095'),
    ('v1', 's->written - s->played))', 's->written - s->copied))'),
    ('v1', 'PCM_CNT_REP_SHIFT)', 'PCM_CNT_UND_SHIFT)'),
    ('v1', 'SND2_CLOSE_DRAIN', 'SND2_CLOSE_ABORT'),
    ('stream', '((ms + 9) / 10 + 3)', '(ms / 10 + 3)'),
    ('stream', 'check(buf, n * stride);', 'check(buf, bytes);'),
    ('stream', 's->state == SND2_ST_DRAIN || !buf', '!buf'),
    ('stream', 'hw_release(d->ctx, leak)', 'hw_release(d->ctx, 0)'),
    ('stream', '++s->gen;', '/* stale generation */'),
    ('stream', 'snd2_seg_reset(s, tick_clock_ms());\n        if (resync)', 'if (resync)'),
    ('stream', 'if (d->be->hw_stop(d->ctx) < 0) {\n                    teardown',
     'snd2_seg_reset(s, now);\n                if (d->be->hw_stop(d->ctx) < 0) {\n                    teardown'),
    ('stream', 'snd2_seg_resync(s, now);', 's->paused = 0; snd2_seg_resync(s, now);'),
    ('stream', 'if (resync) s->restart_pending = 1;', 'if (resync) s->state = SND2_ST_IDLE;'),
    ('stream', 'snd2_seg_resume(s);\n            s->state = SND2_ST_RUN;', 'snd2_seg_resume(s);'),
    ('stream', 'snd2_seg_pause(&d->stream);\n            d->stream.state = SND2_ST_PAUSE;',
     'snd2_seg_pause(&d->stream);'),
    ('stream', 'rc = s->played == s->written ? 0 : OS32_ERR_IO;', 'rc = 0;'),
    ('stream', 'if (!d->writing) snd2_stg_free(s);', 'snd2_stg_free(s);'),
]


def verify(work, mutation=None, diagnostic=False):
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
                    '-ffunction-sections', '-Wl,--gc-sections', '-nostdlib', '-static', '-no-pie', '-D__cdecl=',
                    *(['-DTICK_PROBE', '-DSND2_REFILL_DIAG'] if diagnostic else []), '-I' + str(work), '-I' + str(HAL),
                    *include_flags(ROOT), *sources, ROOT / 'sys/kapi/kapi_snd2.c', ROOT / 'tools/tests/snd2_stream_host.c',
                    '-o', exe], check=True)
    return host_run([str(exe)], cwd=work, timeout=3).returncode


def run(ctx):
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert verify(work) == 0
        assert verify(work, diagnostic=True) == 0
        if ctx.mutate:
            for old, new in [('d->gap_start_ms < 150U', 'd->gap_start_ms < 149U'),
                             ('!d->gap_active &&', '')]:
                assert verify(work, ('stream', old, new), diagnostic=True) == 1
                print('RED runtime', old)
            old = 'stream_lock(TP_SND_ADVANCE)'
            assert verify(work, ('stream', old, 'stream_lock(TP_SND_OTHER)'), diagnostic=True) == 1
            print('RED runtime missing sound advance metric')
            for mutation in MUTATIONS:
                assert verify(work, mutation) == 1, mutation
                print('RED runtime', mutation[1])
    return 0
