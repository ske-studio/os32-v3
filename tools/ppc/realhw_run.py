#!/usr/bin/env python3
"""Capture and assess a real PowerMac session; never starts QEMU."""
import argparse
import errno
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import select
import socket
import struct
import sys
import time
import urllib.parse
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ppc_lan_host as lan
from sc_run import EXPECTED, check_shell
from sd_run import exact

CAMERA = 'http://hight-pc-vj24lfwz1srg:8080'


def run_rh(host, case, evidence):
    """Assess captured terminal output with the existing S-C/S-D contracts."""
    text = evidence['terminal'].replace('\r', '')
    if case == 'nest':
        actual = text.partition('os32> ')[2]
        if actual != EXPECTED:
            raise RuntimeError('N01 LAN transcript: '+repr(actual))
    elif case == 'shell':
        check_shell(text)
    elif case == 'exact':
        exact(text, evidence['command'], evidence['expected'])
    elif case != 'capture':
        raise ValueError('unknown case')
    if any('gap' in line or 'timeout' in line for line in evidence.get('diagnostics', [])):
        raise RuntimeError('capture has gaps or timeouts')
    if host.boot is None:
        raise RuntimeError('no boot beacon')
    return {'case': case, 'pass': True, 'boot_id': host.boot.hex()}


class Capture:
    def __init__(self, packet, path):
        self.packet = packet
        self.local_mac = packet.local_mac
        self.file = path.open('wb')
        self.file.write(struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1))

    def fileno(self):
        return self.packet.fileno()

    def record(self, frame):
        ns = time.time_ns()
        self.file.write(struct.pack('<IIII', ns//1000000000, ns%1000000000//1000, len(frame), len(frame)))
        self.file.write(frame)
        self.file.flush()

    def receive(self):
        frame = self.packet.receive()
        if frame:
            self.record(frame)
        return frame

    def send(self, frame):
        self.packet.send(frame)
        self.record(frame)


def camera_frame(name, out):
    if Path(name).name != name or name in ('', '.', '..'):
        raise ValueError('invalid frame name')
    with urllib.request.urlopen(CAMERA+'/frames/'+urllib.parse.quote(name, safe=''), timeout=15) as response:
        (out/name).write_bytes(response.read())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iface', required=True)
    p.add_argument('--peer', required=True)
    p.add_argument('--image', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--machine', required=True, help='machine identity, OF and PVR')
    p.add_argument('--case', choices=['capture', 'nest', 'shell', 'exact'], default='capture')
    p.add_argument('--command', default='')
    p.add_argument('--expected', default='')
    p.add_argument('--seconds', type=float, default=60)
    p.add_argument('--root')
    p.add_argument('--allow-write', action='append', default=[])
    p.add_argument('--camera-frame', action='append', default=[])
    p.add_argument('--list-frames', action='store_true')
    a = p.parse_args()
    try:
        packet = lan.Packet(a.iface)
    except OSError as exc:
        if exc.errno in (errno.EPERM, errno.EACCES, errno.EAFNOSUPPORT, errno.EPROTONOSUPPORT):
            print('SKIP AF_PACKET: '+str(exc))
            return 0
        print(str(exc), file=sys.stderr)
        return 1
    try:
        a.out.mkdir(parents=True, exist_ok=False)
        capture = Capture(packet, a.out/'capture.pcap')
        if a.list_frames:
            with urllib.request.urlopen(CAMERA+'/api/frames', timeout=15) as response:
                (a.out/'camera-frames.json').write_bytes(response.read())
        for name in a.camera_frame:
            camera_frame(name, a.out)
        class Display(io.BytesIO):
            def write(self, data):
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
                return super().write(data)

        display = Display()
        diagnostics = []
        host = lan.Host(capture, lan.mac(a.peer), a.out, output=diagnostics.append,
                        root=a.root, allow_write=a.allow_write)
        terminal = lan.Terminal(host, display=display, diagnostic=diagnostics.append)
        host.terminal = terminal
        opened = False
        deadline = time.monotonic()+a.seconds
        stdin = sys.stdin.fileno()
        while time.monotonic() < deadline:
            ready, _, _ = select.select([capture] + ([stdin] if stdin is not None else []), [], [], min(.1, max(0, deadline-time.monotonic())))
            if capture in ready:
                frame = capture.receive()
                host.receive(frame)
                terminal.receive(frame)
                if host.boot is not None and not opened:
                    terminal.open()
                    opened = True
            if stdin is not None and stdin in ready:
                data = os.read(stdin, 4096)
                if data:
                    terminal.send_bytes(data)
                else:
                    stdin = None
            host.expire()
        text = display.getvalue().decode('utf-8', 'replace')
        (a.out/'terminal.raw').write_bytes(display.getvalue())
        (a.out/'diagnostics.log').write_text('\n'.join(diagnostics)+'\n')
        evidence = dict(terminal=text, command=a.command, expected=a.expected, diagnostics=diagnostics)
        try:
            result = run_rh(host, a.case, evidence)
        except (RuntimeError, AssertionError) as exc:
            result = dict(case=a.case, **{'pass': False}, error=str(exc), boot_id=host.boot.hex() if host.boot else None)
        result.update(image_sha256=hashlib.sha256(a.image.read_bytes()).hexdigest(), machine=a.machine,
                      time_utc=datetime.now(timezone.utc).isoformat(), camera_frames=a.camera_frame,
                      pcap='capture.pcap', raw_log='terminal.raw')
        (a.out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        capture.file.close()
        print(json.dumps(result, indent=2))
        return 0 if result['pass'] else 1
    except (OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    finally:
        packet.sock.close()


if __name__ == '__main__':
    sys.exit(main())
