"""Run the guest measurement modes with controlled stream and capture markers."""
from pathlib import Path
import subprocess
import tempfile

import host32

CHECK = dict(name='snd2-guest-host', tier=4, needs=[], budget=3,
             covers=['tools/tests/host32.py', 'tools/tests/test_snd2_guest.py', 'tools/tests/snd2_guest_host.c',
                     'userland/tests/snd2_test.c', 'sound/hal/snd2_refill_diag.h', 'sdk/include/os32/*.h'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §13-3'])


def run(ctx):
    source = (ctx.root / 'userland/tests/snd2_test.c').read_text()
    mutants = [
        ('pos.epoch != previous.epoch + 1U', '0'),
        ('pos.played <= recovered.played', 'pos.played < recovered.played'),
        ('!changed || pos.epoch', '0 || pos.epoch'),
        ('samples[1] = -23456;', 'samples[1] = 0;'),
        ('if (!marked || pos.underruns)', 'if (pos.underruns)'),
    ]
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        (work / 'string.h').write_text('int strcmp(const char *, const char *);\n')
        for mutation in [None] + (mutants if ctx.mutate else []):
            guest = source
            if mutation:
                old, new = mutation
                assert source.count(old) == 1
                guest = source.replace(old, new)
            # snd2_test.c reaches the diagnostic header relative to userland/tests/.
            guest = guest.replace('#include "../../sound/hal/snd2_refill_diag.h"',
                                  '#include "' + str(ctx.root / 'sound/hal/snd2_refill_diag.h') + '"')
            (work / 'guest.inc').write_text(guest)
            exe = work / 'guest'
            subprocess.run(['gcc', '-m32', '-static', '-ffreestanding', '-fno-pie', '-no-pie',
                            '-nostdlib', '-std=gnu11', '-O2', '-D__cdecl=', '-I', td,
                            '-I', str(ctx.root / 'sdk/include/os32'),
                            str(ctx.root / 'tools/tests/snd2_guest_host.c'),
                            '-o', str(exe)], check=True)
            rc = host32.run([str(exe)], cwd=work, timeout=5).returncode
            assert rc == int(bool(mutation)), (mutation, rc)
            print('RED runtime ' + mutation[0] if mutation else 'GREEN guest')
    return 0
