#!/usr/bin/env python3
"""Check KAPI_SPEC §4-8 through the PowerMac framebuffer console over link-SH/RSH."""
import argparse
import os
from pathlib import Path
import socket
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
from kboot_accept import build, mutant_settings
from link_sh_run import Session
from run import require
from test_kapi_binding import generate
from usb_run import rsh_command


def image(folder, case):
    folder.mkdir()
    kernel = folder / 'kernel'
    user = folder / 'user'
    settings = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
                'KB_ROOT_BINS=' + str(user / 'celltest.bin')]
    if case == 'scroll-two':
        settings += mutant_settings(folder, case, 'kernel/console/fbcon.c',
                                    'fbcon_scroll_n(c, rows, 1);',
                                    'fbcon_scroll_n(c, rows, 2);')
    if case == 'putchar-absent':
        kernel.mkdir()
        gen = generate(kernel, case)
        (gen / 'stamp').touch()
    return build(kernel, *settings)


def observe(elf, out, case, expected):
    session = Session(elf, '7450', out, 180)
    try:
        session.start()
        session.wait(session.ready, 'console cell probe CUI READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'console probe mode')
        serial = out / 'serial-a.log'
        before = serial.read_text(errors='replace')
        require('FB text ready' in before, 'framebuffer text console attached')
        body = rsh_command(session, '/bin/celltest.bin')
        status = rsh_command(session, 'echo $?')
        code = str(expected).encode()
        require(body == b'celltest=' + code + (b'\n' if expected else b'\n\n') and
                status == code + b'\n' and
                'FATAL' not in serial.read_text(errors='replace')[len(before):],
                f'{case}: unexpected probe result {body!r}, status {status!r}')
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'console probe QEMU exit')
        print(('RED' if expected else 'GREEN') + f' {case} rc={expected}', flush=True)
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    if not args.build_only:
        try:
            for kind in (socket.SOCK_DGRAM, socket.SOCK_STREAM):
                with socket.socket(socket.AF_INET, kind) as probe:
                    probe.bind(('127.0.0.1', 0))
        except OSError as exc:
            print(f'未 ppc-console-cells: AF_INET unavailable: {exc}')
            return 3
    base = Path(os.environ.get('TMPDIR', '/tmp')).resolve() / 'ppc-console-cells'
    base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    print(f'evidence={work}', flush=True)
    cases = [('normal', 0)]
    if args.mutate:
        cases += [('scroll-two', 14), ('putchar-absent', 11)]
    for case, expected in cases:
        folder = work / case
        elf = image(folder, case)
        if args.build_only:
            print(f'BUILT {case}', flush=True)
        else:
            observe(elf, folder / 'guest', case, expected)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
