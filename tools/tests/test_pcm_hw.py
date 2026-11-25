"""S9 device operations with the real S1–S5 core and existing CS4231 port model."""
from pathlib import Path
import subprocess
import tempfile

from test_pcm_cs4231 import FLAGS, INCLUDES, ROOT

CHECK = dict(name='pcm-hw-host', tier=4, needs=[], budget=6,
             covers=['drivers/sound/cs4231/*.[ch]', 'sound/hal/*.[ch]', 'sys/include/av_contract.h',
                     'tools/tests/pcm_hw_host.c', 'tools/tests/test_pcm_hw.py',
                     'tools/tests/test_pcm_cs4231.py', 'tools/tests/mutpar.py', 'tools/tests/pcm_cs4231_host.c',
                     'tools/tests/pcm_hostshim/io.h', 'tools/tests/kernel_includes.py',
                     'sdk/include/os32/*.h', 'build/config.mk'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §3・§12 S9・§13-2'])

MUTATIONS = [
    ('pcm_hw.c', 'if (!rc) pcm_module_refill_tick(dev);', 'if (!rc) pcm_module_refill_tick(-1);'),
    ('pcm_cs4231.c', '++hal_ticks;', '++hal_pi;'),
    ('pcm_cs4231.c', 'hal_max_us = now - hal_last_us;', 'hal_max_us = 0;'),
    ('pcm_cs4231.c', 'if (s_in_advance) {', 'if (0) {'),
    ('pcm_cs4231.c', 'if (irq_unregister(PCM_IRQ, pcm_irq, 0) != 0)',
     'if (irq_unregister(PCM_IRQ, pcm_irq, 0) != 0 && 0)'),
    ('pcm_hw.c', '.volume_steps = PCM_DA_ATT_MAX + 1', '.volume_steps = PCM_DA_ATT_MAX'),
    ('pcm_hw.c', 'attenuation(right)', 'attenuation(left) + 0 * right'),
    ('pcm_cs4231.c', '(u8)((s_hal_period - 1) >> 8)', '(u8)(s_hal_period >> 8)'),
    ('pcm_cs4231.c', 'if (!pcm_codec_ack()) return IRQ_NONE;', 'if (!pcm_codec_ack()) return IRQ_HANDLED;'),
    ('pcm_cs4231.c', 'if (!s_claimed && !s_mce_busy) pcm_hal_advance(PCM_FROM_TICK);',
     'if (!s_claimed && g_pcm.state == PCM_ST_OPEN) pcm_hal_advance(PCM_FROM_TICK);'),
    ('pcm_cs4231.c', 'if (!pcm_abort_proven()) {', 'if (pcm_abort_proven() && 0) {'),
    ('pcm_cs4231.c', '        s_claimed = 1;\n        return OS32_ERR_IO;',
     '        return OS32_ERR_IO;'),
]


def run(ctx):
    driver = ROOT / 'drivers/sound/cs4231'
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        for mutation in [None] + (MUTATIONS if ctx.mutate else []):
            for unit in ('pcm_hw.c', 'pcm_cs4231.c'):
                source = (driver / unit).read_text()
                if mutation and mutation[0] == unit:
                    _, old, new = mutation
                    assert source.count(old) == 1, mutation
                    source = source.replace(old, new)
                (work / unit).write_text(source)
            fixture = (ROOT / 'tools/tests/pcm_cs4231_host.c').read_text()
            fixture = fixture.replace('../../sys/dev/sound/cs4231/pcm_cs4231.c', 'pcm_cs4231.c')
            fixture = fixture.replace('../../sys/dev/sound/cs4231/pcm_cs4231_math.c',
                                      str(driver / 'pcm_cs4231_math.c'))
            (work / 'pcm_cs4231_host.c').write_text(fixture)
            (work / 'test.c').write_text((ROOT / 'tools/tests/pcm_hw_host.c').read_text())
            exe = work / 'test'
            core = [ROOT / f'sound/hal/snd2_{unit}.c' for unit in ('reg', 'caps', 'stream', 'stg', 'seg')]
            subprocess.run(['gcc', '-DSND2_REFILL_DIAG', *FLAGS, *INCLUDES, '-I' + str(ROOT), str(work / 'test.c'),
                            str(work / 'pcm_hw.c'), driver / 'pcm_body.c', *core, '-o', exe], check=True)
            if not mutation:
                subprocess.run(['qemu-x86_64', exe, 'resync-fault'], check=True, timeout=10)
            result = subprocess.run(['qemu-x86_64', exe], capture_output=True, timeout=10)
            if result.returncode != int(bool(mutation)) or (mutation and b'FAIL ' not in result.stderr):
                raise AssertionError((mutation, result))
            print('runtime RED ' + mutation[1] if mutation else 'PASS S9 paired body + stream core')
    return 0
