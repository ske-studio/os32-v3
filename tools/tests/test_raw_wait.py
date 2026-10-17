"""T4b-7: raw waits/normal controls and specifically attributed runtime RED.

Reuse the real driver/wrapper ILP32 fixture, actual SQLite stack/guard and R1
exec landings. Device IRQ/input and CPU transitions are their explicit seams;
this check does not claim to execute the NP21/W guest acceptance matrix.
"""
import argparse
import pathlib
import subprocess
import tempfile

import host32
import test_exec_r1 as lifecycle
import test_kapi_audit as audit
import test_sqlite_engine as engine

# Each mutation must reach this assertion, rather than just fail somewhere.
CASES = {
    'kbd char wait': 'FAIL input CUI abort',
    'kbd key wait': 'FAIL input CUI abort',
    'kbd gui wait': 'FAIL input GUI fallback STOP',
    'ime char cleanup': 'FAIL IME char STOP cancels composition',
    'ime key cleanup': 'FAIL IME key STOP cancels composition',
    'serial receive probe': 'FAIL serial abort',
    'mml wait probe': 'FAIL mml wait abort',
    'mml key off': 'FAIL mml sustained stop key off',
    'shared TX abort regression': 'FAIL USER internal logs and frames never abort',
    'shared puts abort regression': 'FAIL USER internal logs and frames never abort',
}


def guest_fixture():
    root = pathlib.Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='raw-wait-guest-') as directory:
        tmp = pathlib.Path(directory)
        source = (root / 'userland/tests/raw_wait_test.c').read_text()
        (tmp / 'guest_source.inc').write_text(source.replace('#include <string.h>', ''))
        exe = tmp / 'test'
        subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-ffreestanding',
                        '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                        '-I' + str(tmp), '-I' + str(root / 'sdk/include/os32'),
                        '-I' + str(root / 'include'), '-I' + str(root / 'lib'),
                        str(root / 'tools/tests/raw_wait_guest_host.c'),
                        str(root / 'lib/kstring.c'), str(root / 'lib/kstring_c.c'),
                        '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True, text=True, timeout=30)
        print(result.stdout + result.stderr, end='', flush=True)
        assert result.returncode == 0, result.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    guest_fixture()
    audit.run('qemu')
    result = engine.run('qemu')
    print(result.stdout + result.stderr, end='', flush=True)
    assert result.returncode == 0, result.returncode
    status, log = lifecycle.run()
    assert status == 'pass', (status, log)
    print('PASS raw waits: real exec cleanup once / parent esp0+caller / next KAPI / USER heap')
    if args.mutate:
        for name, expected in CASES.items():
            mutation = next(m for m in audit.MUTATIONS if m[0] == name)
            audit.run('qemu', mutation, expected=expected)
        result = engine.run('qemu', 'early STOP')
        assert result.returncode == 1 and 'FAIL: !sqlite_engine_active && !kills' in result.stdout, (
            result.returncode, result.stdout, result.stderr)
        print('RED runtime: SQLite STOP before leave')
        print(f'PASS {len(CASES) + 1} raw-wait mutations: named runtime RED, compile failures 0')


if __name__ == '__main__':
    main()
