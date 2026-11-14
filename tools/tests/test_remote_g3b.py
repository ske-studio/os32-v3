#!/usr/bin/env python3
"""Check the gshell startup wiring and exercise LAN without an initialized UART."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

CHECK = dict(name='remote-g3b', tier=1, needs=[], budget=20, covers=[
    'userland/gshell/**', 'sdk/rust/os32api/**', 'userland/lib/font/**',
    'tools/tests/os32api_host.py', 'tools/tests/g16_fixture.py',
    'tools/fetch_fonts.py', 'tools/gen_g16.py', 'tools/g16/**',
    'assets/fonts/**', 'assets/ipadic/*.csv', 'include/remote_control.h',
    'include/callconv.h', 'include/abi.h', 'arch/x86/arch_abi.h',
    'net/link.h', 'exec/shell_service.h', 'sdk/include/os32/**',
    'tools/tests/test_remote_g3b.py'])
ROOT = Path(__file__).resolve().parents[2]
SERIAL = '    gserial::open(st, serial_timeout);\n'
LAN = '    gserial::open_lan(st, serial_timeout);\n'


def check(source):
    # Restrict the check to the real startup function and require adjacent calls.
    startup = source.split('fn run_standalone(', 1)[0]
    assert startup.count(SERIAL) == 1 and startup.count(LAN) == 1
    assert SERIAL + LAN in startup


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'userland/gshell/src/lib.rs').read_text()
    check(source)
    rc = subprocess.call([sys.executable, '-B', 'userland/gshell/host/integration.py'],
                         cwd=ROOT, env=dict(os.environ, MUTATE='0'))
    if rc:
        return rc
    if args.mutate:
        for name, replacement in [('LAN before serial', LAN + SERIAL),
                                  ('LAN omitted', SERIAL)]:
            assert source.count(SERIAL + LAN) == 1
            try:
                check(source.replace(SERIAL + LAN, replacement))
            except AssertionError:
                print('RED:', name, flush=True)
            else:
                print('SURVIVED:', name, flush=True)
                return 1
    return 0


def run(ctx):
    cmd = [sys.executable, '-B', 'tools/tests/test_remote_g3b.py']
    if ctx.mutate:
        cmd.append('--mutate')
    return subprocess.call(cmd, cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
