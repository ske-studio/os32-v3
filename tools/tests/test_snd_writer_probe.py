"""Foreground writer timestamps with a deterministic calibrated clock."""
import subprocess
import tempfile
from pathlib import Path

CHECK = dict(name='snd-writer-probe-host', tier=4, needs=[], budget=2,
             covers=['platform/pc98/snd_writer_probe.c', 'platform/pc98/tsc_clock.h',
                     'include/snd_writer_probe.h', 'include/types.h',
                     'tools/tests/snd_writer_probe_host.c', 'tools/tests/test_snd_writer_probe.py'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §13-3'])


def run(ctx):
    root = ctx.root
    source = (root / 'platform/pc98/snd_writer_probe.c').read_text()
    with tempfile.TemporaryDirectory() as tmp:
        unit = Path(tmp) / 'probe.c'
        exe = Path(tmp) / 'test'
        for mutation in [None] + (['elapsed >= 10000', 'trace.exit = trace.previous;'] if ctx.mutate else []):
            code = source
            if mutation:
                assert code.count(mutation) == 1
                code = code.replace(mutation, 'elapsed > 10000' if mutation.startswith('elapsed') else 'trace.exit = trace.ready;')
            unit.write_text(code)
            subprocess.run(['gcc', '-std=gnu11', '-DSND2_WRITER_DIAG', '-I', root / 'include',
                            '-I', root / 'platform/pc98', unit, root / 'tools/tests/snd_writer_probe_host.c',
                            '-o', exe], check=True)
            assert subprocess.run([exe]).returncode == int(bool(mutation))
            print('runtime RED ' + mutation if mutation else 'GREEN writer trace')
    return 0
