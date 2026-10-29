"""A2/main, C-only and abort/S0 cases moved verbatim from the C suite."""
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
import mutpar
from test_gfinit_plan_c import ROOT, command, mutation, STAGE_MUTATIONS

EXPECTED = {
    'init': ('INIT PASS',
             'A2 -> main0..4 -> baseline required/record/A4 stable/changing GPIO PASS'),
    'c_only': ('C_ONLY PASS',
               'T2 same-session and C-only share ordered writes, waits, sections and verification masks PASS',
               'T1 baseline once, three E gates, cold latch, CR52/C040, restore and S0 failure PASS'),
    's0': ('S0 PASS',
           'T1 CHECK then S0 local/quiesce/first-send/final-send failure revokes init PASS',
           'T1 C-only each lock write/abort restores per-call CR1F/CR11/CR44 originals PASS'),
}


def check_output(stage, text):
    assert 'FAIL' not in text, text
    for line in EXPECTED[stage]:
        assert line in text, (stage, line, text)


def main():
    with tempfile.TemporaryDirectory(prefix='gfinit-stages-') as td:
        exe = Path(td) / 'host'
        for stage in EXPECTED:
            subprocess.run(command(exe, stage), check=True, cwd=ROOT)
            result = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True)
            print(result.stdout.decode(), end='')
            if result.returncode:
                return result.returncode
            check_output(stage, result.stdout.decode())
    if '--mutate' in sys.argv:
        # The control uses the same stage compiler/runner as its mutations.
        return int(bool(mutpar.run_with_control(mutation, STAGE_MUTATIONS,
                        ('kdebug/gfinit_plan_c.c', 'control', '', '', 'c_only'))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
