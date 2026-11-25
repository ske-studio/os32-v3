"""Banked FM backend, using a scripted PC-98 bus."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32

CHECK = dict(name='opn-chip-host', tier=1, needs=[], budget=2,
    covers=['sys/dev/sound/fm/opn_chip.c', 'sys/dev/sound/fm/fm.h',
            'sys/dev/sound/fm/opn_chip.h',
            'sys/dev/sound/fm/opn_probe.h', 'tools/tests/opn_probe_hostshim/io.h',
            'sys/hal/sound/snd2_int.h', 'sys/include/av_contract.h', 'sdk/include/os32/*.h',
            'sys/include/fm_contract.h', 'sys/include/types.h', 'sys/include/callconv.h',
            'tools/tests/test_opn_chip.py', 'tools/tests/opn_chip_host.c',
            'tools/tests/opn_chip_hostshim/io.h', 'tools/tests/host32.py'],
    basis=['docs/decisions/sound/AV_HAL_DESIGN.md §13',
           'docs/decisions/sound/SYNTH_OPNA_DESIGN.md §6'])

ROOT = Path(__file__).resolve().parents[2]
FM = ROOT / 'drivers/sound/fm'
TESTS = ROOT / 'tools/tests'


def verify(source, exe, expected):
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Werror',
                    '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-I', TESTS / 'opn_chip_hostshim', '-I', FM, '-I', ROOT, '-I', ROOT / 'sdk/include/os32', '-I', ROOT / 'include',
                    source, TESTS / 'opn_chip_host.c', '-o', exe], check=True)
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        result = host32.run([str(exe)], runner=runner, timeout=5)
        assert result.returncode == expected, (runner, result.returncode, expected)


def main():
    source = FM / 'opn_chip.c'
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / 'probe'
        verify(source, exe, 0)
        print('PASS opn_chip_host')
        if '--mutate' in sys.argv:
            for old, new, rc in [
                    ('value = (value & 0x3f) | SSG_MIXER_IO_FLAG', 'value = value', 4),
                    ('value &= ~3u', 'value &= ~0u', 11),
                    ('                continue;', '                return i;', 3),
                    ('        if (w[i].bank >= count)', '        if (i == 0 && w[i].bank >= count)', 1),
                    ('count == 1 && c->shared_irq', 'count == 1', 6),
                    ('c->irq_shadow & ~3u', 'c->irq_shadow', 9),
                    ('bank * 4 + ch', 'ch', 9),
                    ('return ready() ? 0 : OS32_ERR_IO', 'return 0', 17)]:
                body = source.read_text()
                assert body.count(old) == 1, old
                mutant = Path(td) / 'mutant.c'
                mutant.write_text(body.replace(old, new))
                verify(mutant, exe, rc)
                print('MUTATE RED', old, 'case', rc)


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import subprocess
    argv = ['python3', '-B', 'tools/tests/test_opn_chip.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, close_fds=False,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
