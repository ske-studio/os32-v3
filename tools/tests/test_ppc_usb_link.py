#!/usr/bin/env python3
"""PPC_P5_REST_DESIGN §2 usb_run・§5 U; PM U-c1: USB-only failure."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools/ppc'), str(ROOT/'tools')]
import usb_run

CHECK = dict(name='ppc-usb-link-meter', tier=1, budget=10, needs=['make:ppc-ci-image'],
    covers=['sys/kern/exec/exec.c', 'tools/ppc/ppc_shell_flow.inc', 'userland/shell/main.c', 'userland/shell/rshell.c', 'tools/tests/test_ppc_usb_link.py', 'tools/ppc/usb_run.py',
            'tools/ppc/kboot_accept.py', 'tools/ppc/kboot_run.py',
            'tools/ppc/run.py', 'tools/ppc/kboot_mutations.py',
            'platform/powermac/**', 'arch/ppc/**', 'sys/platform/powermac/ppc_kernel.c',
            'tools/ppc/**', 'tools/*.py', 'sys/fs/serialfs/serialfs_persistent.c',
            'sys/kern/console/con_sink.c', 'sys/kern/console/console.c',
            'sys/kern/mm/kmalloc.c', 'userland/shell/rshell_lan.c'],
    basis=['PPC_P5_REST_DESIGN §2・§5', 'PM U-c1 (2026-11-16)'])

# Recorded serial marker shapes; no-service must leave the other services live.
BASE = 'USB HC bdf=00:06.0\nUSB TAKEOVER quiet\nKBOOT READY\n'
MOUSE = 'USB DEV addr=1 vid:pid=0627:0001 class=03 subclass=01 protocol=02\n'
KBD = 'USB DEV addr=2 vid:pid=0627:0001 class=03 subclass=01 protocol=01\n'


def exercise(verdict):
    fixtures = [
        ('mouse enumeration', BASE, True, True, 'RED'),
        ('keyboard enumeration', BASE+MOUSE, True, True, 'RED'),
        ('mouse enumeration', BASE, False, True, 'wrong-reason'),
        ('mouse enumeration', BASE, True, False, 'wrong-reason'),
        ('mouse enumeration', BASE+'usb: HC dead\n', True, True, 'wrong-reason'),
        ('RSH READY', BASE, True, True, 'wrong-reason'),
        ('keyboard enumeration', BASE+MOUSE+KBD, True, True, 'wrong-reason'),
        ('mouse enumeration', BASE+'USB KEY src=1 sc=1d down=1\n', True, True, 'wrong-reason'),
        ('mouse enumeration', BASE+'USB MOUSE src=1 dx=1\n', True, True, 'wrong-reason'),
        (None, BASE+MOUSE+KBD, True, True, 'SURVIVED'),
        # Survival of LOG/RSH alone never proves a USB regression.
        (None, BASE, True, True, 'SURVIVED'),
    ]
    for reason, serial, log, rsh, expected in fixtures:
        assert verdict(reason, serial, log, rsh) == expected, (reason, serial, log, rsh, expected)


def main():
    exercise(usb_run.link_usb_verdict)
    with tempfile.TemporaryDirectory() as folder:
        src, dest = usb_run.mutation(Path(folder), 'no-service')
        original = (ROOT/src).read_text()
        assert original.count('    usb_service();') == 1
        assert dest.read_text() == original.replace('    usb_service();',
                                                   '    /* mutation: omit USB service */')
    if '--mutate' in sys.argv:
        source = Path(usb_run.__file__).read_text()
        for before, after in [
            ('not log_alive or not rsh_alive', 'False'),
            ("or 'usb: HC dead' in text", ''),
            ("reason == 'mouse enumeration'", 'False'),
            ("reason == 'keyboard enumeration'", 'False'),
            ("and 'USB KEY src=1' not in text", ''),
            ("and 'USB MOUSE' not in text:\n        return 'RED'", ":\n        return 'RED'"),
            ("return 'SURVIVED' if reason is None else 'wrong-reason'", "return 'RED'"),
        ]:
            assert source.count(before) == 1, before
            scope = dict(__name__='usb_mutant', __file__=usb_run.__file__)
            exec(compile(source.replace(before, after), usb_run.__file__, 'exec'), scope)
            try: exercise(scope['link_usb_verdict'])
            except AssertionError: print('RED runtime USB verdict: '+before)
            else: raise AssertionError('SURVIVED '+before)
    print('PASS USB-only RED, wrong-reason and survival fixtures')
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
