#!/usr/bin/env python3
"""Capture and assess a real PowerMac session; never starts QEMU."""
import argparse
import base64
import threading
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
from sc_run import EXPECTED, LINK_NEST_EXPECTED, check_shell
from kboot_run import validate
from ppc_link_transport import Host as LinkHost
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



def link_script(case, command='', expected=''):
    return {'capture': [], 'nest': [('/bin/nest.bin', LINK_NEST_EXPECTED), ('echo $?', b'5\n')],
            'shell': [('echo x > /tmp/t', b''), ('cat /tmp/t', b'x\n'),
                      ('cat /tmp/none', b'cat: /tmp/none: No such file or directory\n')],
            'exact': [(command, expected.encode())]}[case]


def link_body(line, reply):
    if not (reply.get('ok') and reply.get('state') == 'done' and reply.get('complete')
            and reply.get('drop') == 0):
        raise RuntimeError('incomplete RSH OUT')
    body = base64.b64decode(reply['body_b64'], validate=True)
    prefix = b'> '+line.encode()+b'\n'
    if not body.startswith(prefix) or not body.endswith(b'\x04'):
        raise RuntimeError('RSH echo/EOT')
    return body[len(prefix):-1]


def read_pcap(path):
    data = path.read_bytes()
    if len(data) < 24 or data[:24] != struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1):
        raise RuntimeError('invalid pcap header')
    offset = 24
    while offset < len(data):
        if len(data)-offset < 16:
            raise RuntimeError('short pcap record')
        sec, usec, size, original = struct.unpack_from('<IIII', data, offset)
        offset += 16
        if size != original or size < 14 or size > 65535 or usec >= 1000000 or len(data)-offset < size:
            raise RuntimeError('truncated pcap frame')
        yield sec+usec/1000000, data[offset:offset+size]
        offset += size


def assess_link(case, evidence, pcap, peer, command='', expected=''):
    start, end = evidence['probes']
    if (not start.get('ok') or not end.get('ok') or not end.get('hello') or
            start.get('agent_generation') is None or start.get('peer') != peer or
            any(start.get(k) != end.get(k) for k in ('agent_generation', 'peer'))):
        raise RuntimeError('agent generation/peer/HELLO')
    logs = evidence['logs']
    if not logs or [r['seq'] for r in logs] != list(range(len(logs))):
        raise RuntimeError('LOG sequence from zero')
    if any(r['drop_records'] != 0 or r['drop_bytes'] != 0 for r in logs):
        raise RuntimeError('LOG drop')
    if len({r['boot'] for r in logs}) != 1:
        raise RuntimeError('LOG boot changed')
    body = b''.join(base64.b64decode(r['text_b64'], validate=True) for r in logs)
    validate(body.decode('ascii'), '', True)
    guest = lan.mac(peer)
    hellos, session, last = set(), None, None
    calls, responses, tx0 = set(), {}, 0
    for stamp, frame in read_pcap(pcap):
        if not evidence['begin'] <= stamp <= evidence['end']:
            raise RuntimeError('pcap outside evaluation window')
        outgoing = frame[6:12] == guest
        if outgoing and frame[12:14] == b'\x88\xb6':
            tx0 += 1
        if frame[12:14] != b'\x88\xb5' or not (outgoing or frame[:6] == guest):
            continue
        if len(frame) < 34:
            raise RuntimeError('short link header')
        op, flags, epoch, seq, ack, size, rid, sess = struct.unpack_from('<BBHIIHIH', frame, 14)
        if size > 1400 or len(frame) < 34+size:
            raise RuntimeError('short link payload')
        payload = frame[34:34+size]
        if op == 1:
            hellos.add(outgoing)
        if outgoing and True in hellos:
            if last is not None and (stamp < last or stamp-last > 2):
                raise RuntimeError('ALIVE gap')
            last = stamp
            if op != 1:
                if session is not None and session != sess:
                    raise RuntimeError('link session changed')
                session = sess
        if outgoing and op == 2 and payload.startswith(b'SFS CALL '):
            calls.add((sess, rid))
        if not outgoing and op == 3 and not flags & 1:
            if len(payload) != 6:
                raise RuntimeError('bad RESP')
            responses[(sess, rid)] = struct.unpack_from('<H', payload)[0]
    if hellos != {True, False} or session is None or last is None or evidence['end']-last > 2:
        raise RuntimeError('missing HELLO/ALIVE/session')
    script = link_script(case, command, expected)
    results = evidence['rsh']
    if len(results) < len(script):
        raise RuntimeError('missing RSH record')
    for i, record in enumerate(results):
        actual = link_body(record['line'], record['result'])
        if i < len(script):
            line, output = script[i]
            if actual != output:
                raise RuntimeError('RSH body mismatch')
            if record['line'] != line:
                raise RuntimeError('RSH command order')
            # Fixed RSH framing removed above; reuse S-D's exact semantic comparison.
            exact(line+'\n'+actual.decode('utf-8')+'sh> ', line, output.decode('utf-8'))
    statuses = [responses.get(key) for key in sorted(calls)]
    if case == 'exact' and any(status != 200 for status in statuses):
        raise RuntimeError('SFS response missing/non-200')
    return dict(case=case, **{'pass': True}, session=session, guest_pln1_tx=tx0,
                sfs_calls=len(calls), sfs_response_statuses=statuses)


class PassivePacket:
    def __init__(self, iface):
        self.sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
        try:
            self.sock.bind((iface, 0))
            self.local_mac = self.sock.getsockname()[4]
        except BaseException:
            self.sock.close()
            raise

    def fileno(self):
        return self.sock.fileno()

    def receive(self):
        return self.sock.recv(65535)


def link_main(a):
    packet = None
    host = LinkHost()
    stop = threading.Event()
    errors = []
    evidence = dict(probes=[], logs=[], rsh=[], begin=time.time())
    result = dict(case=a.case, **{'pass': False})
    try:
        packet = PassivePacket(a.iface)
        a.out.mkdir(parents=True, exist_ok=False)
        capture = Capture(packet, a.out/'capture.pcap')
        def pump():
            try:
                while not stop.is_set():
                    if select.select([capture], [], [], .05)[0]:
                        capture.receive()
            except Exception as exc:
                errors.append(str(exc))
        worker = threading.Thread(target=pump)
        worker.start()
        try:
            evidence['probes'].append(host.probe())
            host.subscribe_log()
            if a.list_frames:
                with urllib.request.urlopen(CAMERA+'/api/frames', timeout=15) as response:
                    (a.out/'camera-frames.json').write_bytes(response.read())
            for name in a.camera_frame:
                camera_frame(name, a.out)
            commands = [line for line, _ in link_script(a.case, a.command, a.expected)]
            deadline = time.monotonic()+a.seconds
            stdin, pending = sys.stdin.fileno(), b''
            while time.monotonic() < deadline:
                transport = host.transport
                if transport.events or select.select([transport.sock], [], [], 0)[0]:
                    event = host.receive()
                    if event is not None:
                        event.pop('payload')
                        evidence['logs'].append(event)
                    continue
                if commands:
                    probe = host.probe()
                    if not (probe.get('hello') and (probe.get('ready') or {}).get('mode') == 'cui'):
                        stop.wait(.05)
                        continue
                    line = commands.pop(0)
                    reply = host.send_line(line, min(15, max(.01, deadline-time.monotonic())))
                    evidence['rsh'].append(dict(line=line, result=reply))
                    continue
                if stdin is not None and select.select([stdin], [], [], .05)[0]:
                    chunk = os.read(stdin, 4096)
                    pending += chunk
                    if not chunk:
                        if pending:
                            raise RuntimeError('unfinished stdin line')
                        stdin = None
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n', 1)
                        commands.append(line.decode('utf-8').removesuffix('\r'))
                elif stdin is None:
                    stop.wait(.05)
            evidence['probes'].append(host.probe())
            while host.transport.events:
                event = host.receive()
                event.pop('payload')
                evidence['logs'].append(event)
            if commands or pending:
                raise RuntimeError('unfinished command input')
        finally:
            stop.set()
            worker.join()
            capture.file.close()
            evidence['end'] = time.time()
            (a.out/'link-evidence.json').write_text(json.dumps(evidence, indent=2)+'\n')
        if errors:
            raise RuntimeError('capture error: '+repr(errors))
        result = assess_link(a.case, evidence, a.out/'capture.pcap', a.peer, a.command, a.expected)
    except OSError as exc:
        if packet is None and exc.errno in (errno.EPERM, errno.EACCES, errno.EAFNOSUPPORT, errno.EPROTONOSUPPORT):
            required = os.environ.get('OS32_REQUIRE_PPC') == '1'
            print(('FAIL' if required else 'SKIP')+' AF_PACKET: '+str(exc))
            return int(required)
        result['error'] = str(exc)
    except (ValueError, RuntimeError, AssertionError, KeyError, UnicodeError) as exc:
        result['error'] = str(exc)
    finally:
        host.close()
        if packet is not None:
            packet.sock.close()
    result.update(image_sha256=hashlib.sha256(a.image.read_bytes()).hexdigest(), machine=a.machine,
                  time_utc=datetime.now(timezone.utc).isoformat(), camera_frames=a.camera_frame,
                  pcap='capture.pcap', raw_log='link-evidence.json')
    if a.out.is_dir():
        (a.out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    return 0 if result['pass'] else 1


def camera_frame(name, out):
    if Path(name).name != name or name in ('', '.', '..'):
        raise ValueError('invalid frame name')
    with urllib.request.urlopen(CAMERA+'/frames/'+urllib.parse.quote(name, safe=''), timeout=15) as response:
        (out/name).write_bytes(response.read())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--link', action='store_true')
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
    if a.seconds <= 0:
        p.error('--seconds must be positive')
    if a.link:
        return link_main(a)
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
