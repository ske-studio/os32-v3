"""Quiet-time bank-zero FM detection, using a scripted PC-98 bus."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32

CHECK = dict(name='opn-probe-host', tier=1, needs=[], budget=2,
    covers=['drivers/sound/fm/opn_probe.c', 'drivers/sound/fm/fm.h',
            'drivers/sound/fm/opn_probe.h',
            'include/fm_contract.h', 'include/types.h', 'include/callconv.h',
            'tools/tests/test_opn_probe.py', 'tools/tests/opn_probe_host.c',
            'tools/tests/opn_probe_hostshim/io.h', 'tools/tests/host32.py'],
    basis=['docs/decisions/sound/AV_HAL_DESIGN.md §13',
           'docs/decisions/sound/SYNTH_OPNA_DESIGN.md §6'])

ROOT = Path(__file__).resolve().parents[2]
FM = ROOT / 'drivers/sound/fm'
TESTS = ROOT / 'tools/tests'


def verify(source, exe, expected):
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Werror',
                    '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-I', TESTS / 'opn_probe_hostshim', '-I', FM, '-I', ROOT / 'include',
                    source, TESTS / 'opn_probe_host.c', '-o', exe], check=True)
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        result = host32.run([str(exe)], runner=runner, timeout=5)
        assert result.returncode == expected, (runner, result.returncode, expected)


def main():
    source = FM / 'opn_probe.c'
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / 'probe'
        verify(source, exe, 0)
        print('PASS opn_probe_host')
        if '--mutate' in sys.argv:
            for old, new, rc in [
                    ('|| !rose', '|| (rose && 0)', 8),
                    ('int cleared = ready_status() == 0', 'int cleared = ready_status() >= 0', 10),
                    ('inp(OPN_DATA) != id', '(inp(OPN_DATA), 0)', 5),
                    ('OPN_TIMER_STOP, 1', 'OPN_TIMER_STOP, 0', 12),
                    ('0x29, irq_shadow, 1', '0x29, irq_shadow, 0', 12),
                    ('int after = ready_status()', 'int after = 0', 6),
                    ('io_wait_n(20);\n    outp', 'io_wait_n(6);\n    outp', 6)]:
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
    argv = ['python3', '-B', 'tools/tests/test_opn_probe.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, close_fds=False,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
