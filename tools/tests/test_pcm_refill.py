"""T3: libc-free production driver/math, DMA deadlines and refill origin."""
CHECK = dict(name='pcm-refill-host', tier=4, covers=[
    'drivers/sound/cs4231/pcm_refill.h',
    'drivers/sound/cs4231/pcm_cs4231.c',
    'drivers/sound/cs4231/pcm_cs4231.h',
    'drivers/sound/cs4231/pcm_cs4231_math.c',
    'tools/tests/pcm_refill_host.c', 'tools/tests/test_pcm_refill.py',
    'tools/tests/pcm_hostshim/io.h', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'include/pcm_contract.h',
    'include/time_units.h', 'kernel/time/tick_clock.h'], needs=[], budget=3.0)
from pathlib import Path
import subprocess
import sys
import tempfile
from host32 import run as host_run
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
DRIVER = 'drivers/sound/cs4231/pcm_cs4231.c'
HEADER = 'drivers/sound/cs4231/pcm_refill.h'
INPUTS = (DRIVER, HEADER, 'drivers/sound/cs4231/pcm_cs4231.h',
          'drivers/sound/cs4231/pcm_cs4231_math.c')
MUTATIONS = (
    (HEADER, 'return distance <= pcm_refill_threshold(rate);',
     'return distance > pcm_refill_threshold(rate);'),
    (HEADER, 'return PCM_REFILL_MARGIN + pcm_refill_guard_frames(rate);',
     'return pcm_refill_guard_frames(rate);'),
    (HEADER, '#define PCM_REFILL_SAMPLE_US 10001U',
     '#define PCM_REFILL_SAMPLE_US 1001U'),
    (DRIVER, '!pcm_tick_rescue_due(p1, g_pcm.rate)', '0'),
    (DRIVER, 'return pcm_advance(PCM_FROM_IRQ10);',
     'return pcm_advance(PCM_FROM_TICK);'),
    (DRIVER, '(void)pcm_advance(PCM_FROM_TICK);',
     '(void)pcm_advance(PCM_FROM_IRQ10);'),
    (DRIVER, 'have = dma_chan_remaining(PCM_DMA_CHAN, &left, &tc) == 0;',
     'have = 1;'),
    (DRIVER, '(u32)(now - g_pcm.now) <\n'
     '                (PCM_HALF_FRAMES * USEC_PER_SEC) / g_pcm.rate &&', ''),
    (DRIVER, 'pcm_refill_record(&pcm_diag_refill[origin], started);',
     'pcm_refill_record(&pcm_diag_refill[PCM_FROM_IRQ10], started);'),
)


def verify(work, mutation=None):
    for name in INPUTS:
        source = (ROOT / name).read_text()
        if mutation and name == mutation[0]:
            assert source.count(mutation[1]) == 1, mutation
            source = source.replace(mutation[1], mutation[2])
        dest = work / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(source)
    exe = work / 'pcm-refill'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-O2', '-ffreestanding', '-fno-pie',
                    '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
                    '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                    '-D__cdecl=', '-DPCM_REFILL_DIAG',
                    '-I' + str(work),
                    '-I' + str(ROOT / 'tools/tests/pcm_hostshim'),
                    *include_flags(ROOT), str(ROOT / 'tools/tests/pcm_refill_host.c'),
                    '-o', str(exe)], check=True)
    return host_run([str(exe)], cwd=work, timeout=3).returncode


def main(mutate=False):
    with tempfile.TemporaryDirectory(prefix='pcm-refill-') as td:
        work = Path(td)
        assert verify(work) == 0
        print('PASS PCM refill: both rates/states/directions, production math', flush=True)
        if mutate:
            for mutation in MUTATIONS:
                rc = verify(work, mutation)
                assert rc == 1, (mutation, rc)
                print('RED runtime:', mutation[1], 'rc=1', flush=True)
    return 0


def run(ctx):
    return main(ctx.mutate)


if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
