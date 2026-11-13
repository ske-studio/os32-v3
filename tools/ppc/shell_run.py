#!/usr/bin/env python3
"""Exercise the development shell on both PowerPC CPUs."""
import argparse
import io
import os
from pathlib import Path
import re
import select
import shutil
import socket
import subprocess
import sys
import tempfile
import time

from kboot_run import ROOT, ROM, QMP, command, require, serial, validate, lan

ELF = ROOT / 'build/out/ppc-kboot/shell/PPCKERN.ELF'


def accept(cpu, timeout, sb=False):
    base = Path(os.environ['TMPDIR']) / 'shell-run'
    base.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix=cpu+'-', dir=base))
    cd = out / 'cd'
    cd.mkdir()
    shutil.copyfile(ELF, cd / 'PPCKERN.ELF')
    iso = out / 'boot.iso'
    command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
    a, b = out / 'serial-a.log', out / 'serial-b.log'
    a.touch(); b.touch()
    args = ['qemu-system-ppc', *(['-g', '800x600x8'] if os.environ.get('PPC_FB') == '1' else []), '-M', 'mac99', '-cpu', cpu, '-m', '128M',
            '-smp', '1', '-accel', 'tcg', '-bios', str(ROM),
            '-drive', f'file={iso},media=cdrom,id=bootcd', '-boot', 'd',
            '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF',
            '-prom-env', 'auto-boot?=true', *(['-display', 'none'] if os.environ.get('PPC_FB') == '1' else ['-nographic']), '-serial', 'file:'+str(a),
            '-serial', 'file:'+str(b), '-monitor', 'none', '-qmp', 'stdio', '-no-reboot']
    reservation = transport = host = None
    lines = []
    try:
        reservation = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
        transport = lan.UDP(f'127.0.0.1:0,127.0.0.1:{port}')
        host = lan.Host(transport, bytes.fromhex('525400123456'), out/'capture', output=lines.append)
        args += ['-nic', 'none', '-device', 'sungem,netdev=n0,mac=52:54:00:12:34:56',
                 '-netdev', f'socket,id=n0,udp=127.0.0.1:{transport.sock.getsockname()[1]},localaddr=127.0.0.1:{port}']
    except PermissionError:
        if reservation: reservation.close()
        reservation = None
    deadline = time.monotonic() + timeout
    display = io.BytesIO()
    terminal = lan.Terminal(host, display=display, diagnostic=lines.append) if host else None
    q = None
    with (out/'qemu.stderr').open('w') as err:
        if reservation: reservation.close()
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, bufsize=0)
        try:
            q = QMP(p, deadline)
            phase = 0
            offset = 0
            while time.monotonic() < deadline:
                aa, bb = serial(a), serial(b)
                done = validate(aa, bb)
                if host:
                    while select.select([transport], [], [], 0)[0]:
                        frame = transport.receive()
                        host.receive(frame)
                        terminal.receive(frame)
                    host.expire()
                if phase == 0 and done and 'os32> ' in aa and (not host or host.boot):
                    validate(aa, bb, True)
                    print(cpu + ' serial: os32> ', flush=True)
                    if not host:
                        break
                    q.call('eject', {'device': 'bootcd', 'force': True})
                    terminal.open()
                    phase = 1
                elif phase == 1 and terminal.connected:
                    terminal.send_bytes(b'echo hello\n'); phase = 2
                elif phase == 2 and (match := re.search(rb'\nhello\r?\n.*?os32> ', display.getvalue()[offset:], re.S)):
                    offset += match.end(); host.request('clock'); phase = 3
                elif phase == 3 and any(re.match(r'response 1 OK tick=', x) for x in lines):
                    terminal.send_bytes(b'cat /etc/motd\n'); phase = 4
                elif phase == 4 and (match := re.search(rb'OS32 PowerPC shell\r?\n.*?os32> ', display.getvalue()[offset:], re.S)):
                    offset += match.end(); terminal.send_bytes(b'echo a > /tmp/f\n'); phase = 5
                elif phase == 5 and (match := re.search(rb'os32> ', display.getvalue()[offset:])):
                    offset += match.end(); terminal.send_bytes(b'cat /tmp/f\n'); phase = 6
                elif phase == 6 and (match := re.search(rb'\na\r?\n.*?os32> ', display.getvalue()[offset:], re.S)):
                    offset += match.end()
                    if sb:
                        terminal.send_bytes(b'/bin/hello.bin ok\n'); phase = 7
                        continue
                    require(not any('gap' in x or 'timeout' in x for x in lines), 'terminal or diagnostic loss')
                    break
                elif phase in (7, 8, 10) and (match := re.search(rb'\nhello ok\r?\nexit=7\r?\nos32> ', display.getvalue()[offset:])):
                    offset += match.end()
                    if phase == 10:
                        require(not any('gap' in x or 'timeout' in x for x in lines), 'terminal or diagnostic loss')
                        break
                    terminal.send_bytes(b'/bin/hello.bin ok\n' if phase == 7 else b'/bin/fault.bin\n')
                    phase += 1
                elif phase == 9 and (match := re.search(rb'\nexit=-2\r?\nos32> ', display.getvalue()[offset:])):
                    offset += match.end(); terminal.send_bytes(b'/bin/hello.bin ok\n'); phase = 10
                time.sleep(0.01)
            else:
                raise RuntimeError(f'{cpu}: shell acceptance timeout phase={phase}; serial={serial(a)!r}; terminal={display.getvalue()!r}; host={lines!r}')
            q.call('quit')
            p.wait(timeout=2)
            require(p.returncode == 0, 'QEMU exit')
        finally:
            (out/'host.txt').write_text('\n'.join(lines)+'\n')
            if q: q.close()
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2)
                except subprocess.TimeoutExpired: p.kill(); p.wait()
            if transport: transport.sock.close()
    return host is None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--sb', action='store_true', help='also accept S-B Q1/Q2')
    parser.add_argument('--timeout', type=float, default=60)
    args = parser.parse_args()
    skipped = False
    for cpu in (['7450', '7455'] if args.cpu == 'both' else [args.cpu]):
        skipped |= accept(cpu, args.timeout, args.sb)
    if skipped:
        print('SKIP LAN socket rc=3')
        return 3
    print('PASS shell')
    return 0


if __name__ == '__main__':
    sys.exit(main())
