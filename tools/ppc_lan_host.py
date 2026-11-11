#!/usr/bin/env python3
"""Capture PLN1 Ethernet frames and issue read-only diagnostic requests."""
import argparse
import json
import os
import pathlib
import secrets
import select
import socket
import struct
import sys
import time
import termios
import tty

MAGIC = 0x504c4e31
VERSION = 1
ETHERTYPE = 0x88b6
HEADER_BYTES = 80
PAYLOAD_MAX = 256
BEACON, LOG, REQ, RSP = 1, 2, 3, 4
TERM_OPEN, TERM_IN, TERM_OUT, TERM_STATE = 5, 6, 7, 8
STATUS, CLOCK, MEMORY, READ_DIAG, PEEK = 1, 2, 3, 4, 5
STATUS_NAMES = ('OK', 'UNSUPPORTED', 'BAD_LENGTH', 'RANGE', 'CONFLICT', 'STALE', 'BUSY', 'FAULT')
HEADER = struct.Struct('!IBBH16sQQQIHHHHI16s')


def mac(text):
    parts = text.split(':')
    if len(parts) != 6 or any(len(p) != 2 for p in parts):
        raise ValueError('MAC must contain six hex octets')
    return bytes(int(p, 16) for p in parts)


def encode(kind, boot, *, seq=0, drop_records=0, drop_bytes=0, request_id=0,
           opcode=0, status=0, flags=0, nonce=bytes(16), payload=b''):
    if kind not in (BEACON, LOG, REQ, RSP, TERM_OPEN, TERM_IN, TERM_OUT, TERM_STATE) or len(payload) > PAYLOAD_MAX:
        raise ValueError('invalid message')
    if kind in (BEACON, LOG) and nonce != bytes(16):
        raise ValueError('unexpected nonce')
    return HEADER.pack(MAGIC, VERSION, kind, HEADER_BYTES, boot, seq,
                       drop_records, drop_bytes, request_id, opcode, status,
                       len(payload), flags, 0, nonce) + payload


def decode(data):
    if len(data) < HEADER_BYTES:
        raise ValueError('short header')
    (magic, version, kind, header, boot, seq, records, dropped, request_id,
     opcode, status, size, flags, reserved, nonce) = HEADER.unpack_from(data)
    if (magic != MAGIC or version != VERSION or header != HEADER_BYTES or
            kind not in (BEACON, LOG, REQ, RSP, TERM_OPEN, TERM_IN, TERM_OUT, TERM_STATE) or reserved or size > PAYLOAD_MAX or
            size > len(data) - HEADER_BYTES):
        raise ValueError('invalid header')
    # A PLN1 header alone exceeds the minimum Ethernet payload, so no padding is due.
    if len(data) != HEADER_BYTES + size:
        raise ValueError('extra bytes')
    if kind in (BEACON, LOG) and nonce != bytes(16):
        raise ValueError('unexpected nonce')
    return dict(kind=kind, boot=boot, seq=seq, drop_records=records,
                drop_bytes=dropped, request_id=request_id, opcode=opcode,
                status=status, flags=flags, nonce=nonce, payload=data[HEADER_BYTES:])


def endpoint(value):
    host, port = value.rsplit(':', 1)
    return socket.gethostbyname(host), int(port)


class UDP:
    def __init__(self, spec, local_mac=bytes.fromhex("020000000001")):
        local, remote = spec.split(',')
        self.remote = endpoint(remote)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(endpoint(local))
        self.local_mac = local_mac

    def fileno(self):
        return self.sock.fileno()

    def receive(self):
        data, source = self.sock.recvfrom(65535)
        return data if source == self.remote else b''

    def send(self, frame):
        self.sock.sendto(frame, self.remote)


class Packet:
    def __init__(self, iface):
        self.sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETHERTYPE))
        self.sock.bind((iface, ETHERTYPE))
        self.local_mac = self.sock.getsockname()[4]

    def fileno(self):
        return self.sock.fileno()

    def receive(self):
        data, address = self.sock.recvfrom(65535)
        return b'' if address[2] == socket.PACKET_OUTGOING else data

    def send(self, frame):
        self.sock.send(frame)


class Host:
    def __init__(self, io, peer, out, boot=None, *, nonce=None, clock=time.monotonic, output=print):
        self.io, self.peer, self.out = io, peer, pathlib.Path(out)
        self.out.mkdir(parents=True, exist_ok=True)
        self.boot, self.constraint = None, boot
        self.nonce = secrets.token_bytes(16) if nonce is None else nonce
        self.clock, self.output = clock, output
        self.next_id, self.pending = 1, {}
        self.high = None
        self.max_next_seq = 0
        self.seen = set()
        self.generation = 0

    def receive(self, frame):
        if len(frame) < 14 or frame[6:12] != self.peer or frame[12:14] != struct.pack('!H', ETHERTYPE):
            return
        try:
            m = decode(frame[14:])
        except ValueError:
            return
        if m['kind'] in (LOG, BEACON) and frame[:6] not in (self.io.local_mac, b'\xff' * 6):
            return
        if m['kind'] == RSP and frame[:6] != self.io.local_mac:
            return
        boot = m['boot']
        if self.constraint is not None and boot != self.constraint:
            return
        if self.boot is None:
            if m['kind'] != BEACON:
                return
            self.boot = boot
        if boot != self.boot:
            return
        if m['kind'] == RSP:
            pending = self.pending.get(m['request_id'])
            if m['nonce'] != self.nonce or pending is None or m['opcode'] != pending[1]:
                return
            if self.clock() >= pending[0]:
                self.expire()
                return
            del self.pending[m['request_id']]
            self.output(format_response(m))
            return
        if m['kind'] not in (LOG, BEACON):
            return
        seq = m['seq']
        if m['kind'] == LOG:
            if self.high is None and seq > 0:
                self.output(f'initial gap: {seq}')
            elif self.high is not None:
                if seq in self.seen:
                    self.output(f'duplicate: {seq}')
                elif seq < self.high:
                    self.output(f'out of order: {seq}')
                elif seq > self.high + 1:
                    self.output(f'gap: {seq - self.high - 1}')
            self.max_next_seq = max(self.max_next_seq, seq + 1)
            self.seen.add(seq)
            self.high = seq if self.high is None else max(self.high, seq)
            with (self.storage(boot) / (boot.hex() + '.log')).open('ab') as f:
                f.write(m['payload'])
        elif seq < self.max_next_seq:
            self.output('possible restart: sequence rollback')
            self.generation += 1
            self.high = None
            self.max_next_seq = 0
            self.seen.clear()
        elif seq > (0 if self.high is None else self.high + 1):
            self.output(f'beacon gap: {seq - (0 if self.high is None else self.high + 1)}')
        if m['kind'] == BEACON:
            self.max_next_seq = max(self.max_next_seq, seq)
        self.output(f"{('beacon' if m['kind'] == BEACON else 'log')} seq={seq} drop_records={m['drop_records']} drop_bytes={m['drop_bytes']}; loss after last frame unknown until next beacon")
        metadata = dict(seq=seq, drop_records=m['drop_records'], drop_bytes=m['drop_bytes'],
                        time=time.time(), kind='beacon' if m['kind'] == BEACON else 'log')
        with (self.storage(boot) / (boot.hex() + '.jsonl')).open('a', encoding='utf-8') as f:
            f.write(json.dumps(metadata) + '\n')

    def storage(self, boot):
        if not self.generation:
            return self.out
        directory = self.out / f'{boot.hex()}.{self.generation}'
        directory.mkdir(parents=True, exist_ok=True)
        return directory

    def request(self, command):
        if self.boot is None:
            raise ValueError('waiting for beacon')
        words = command.split()
        if not words:
            return
        names = {'status': STATUS, 'clock': CLOCK, 'memory': MEMORY,
                 'read-diag': READ_DIAG, 'peek': PEEK}
        if words[0] not in names:
            raise ValueError('unknown command')
        op = names[words[0]]
        if op <= MEMORY and len(words) == 1:
            payload = b''
        elif op == READ_DIAG and len(words) == 3:
            off, length = (int(x, 0) for x in words[1:])
            if not 1 <= length <= 128 or not 0 <= off <= 256 - length:
                raise ValueError('read-diag range')
            payload = struct.pack('!HH', off, length)
        elif op == PEEK and len(words) == 3:
            va, length = (int(x, 0) for x in words[1:])
            if not 1 <= length <= 256 or not 0 <= va <= 0xffffffff or va + length > 0x100000000:
                raise ValueError('peek range')
            payload = struct.pack('!IHH', va, length, 0)
        else:
            raise ValueError('invalid arguments')
        if self.next_id > 0xffffffff:
            raise OverflowError('request IDs exhausted; end session')
        rid = self.next_id
        self.next_id += 1
        message = encode(REQ, self.boot, request_id=rid, opcode=op, nonce=self.nonce, payload=payload)
        self.io.send(self.peer + self.io.local_mac + struct.pack('!H', ETHERTYPE) + message)
        self.pending[rid] = (self.clock() + 0.5, op)
        self.output(f'request {rid} sent')

    def expire(self):
        now = self.clock()
        for rid, (deadline, _) in list(self.pending.items()):
            if now >= deadline:
                del self.pending[rid]
                self.output(f'request {rid} timeout')



class Terminal:
    def __init__(self, host, *, display=None, diagnostic=None):
        self.host = host
        self.display = display if display is not None else sys.stdout.buffer
        self.diagnostic = diagnostic if diagnostic is not None else lambda text: print(text, file=sys.stderr)
        self.connected = False
        self.in_seq = 0
        self.out_next = 0
        self.state_next = 0

    def open(self):
        self.connected = False
        self.in_seq = self.out_next = self.state_next = 0
        self.host.nonce = secrets.token_bytes(16)
        self.send(TERM_OPEN)

    def send(self, kind, *, seq=0, payload=b''):
        h = self.host
        message = encode(kind, h.boot, seq=seq, nonce=h.nonce, payload=payload)
        h.io.send(h.peer + h.io.local_mac + struct.pack('!H', ETHERTYPE) + message)

    def send_bytes(self, data):
        if not self.connected:
            return
        data = data.replace(b'\r', b'\n')
        for offset in range(0, len(data), PAYLOAD_MAX):
            self.send(TERM_IN, seq=self.in_seq, payload=data[offset:offset + PAYLOAD_MAX])
            self.in_seq += 1

    def receive(self, frame):
        h = self.host
        if (len(frame) < 14 or frame[:6] != h.io.local_mac or
                frame[6:12] != h.peer or frame[12:14] != struct.pack('!H', ETHERTYPE)):
            return
        try:
            m = decode(frame[14:])
        except ValueError:
            return
        if m['boot'] != h.boot or m['nonce'] != h.nonce:
            return
        if m['kind'] == TERM_STATE and len(m['payload']) == 24:
            self.connected = True
            in_next, in_drop, in_bytes = struct.unpack('!QQQ', m['payload'])
            if m['seq'] > max(self.out_next, self.state_next):
                self.diagnostic(f"output gap: {m['seq'] - max(self.out_next, self.state_next)}")
            self.state_next = max(self.state_next, m['seq'])
            self.diagnostic(f"state: in_next={in_next} in_drop_frames={in_drop} in_drop_bytes={in_bytes} output_drop_frames={m['drop_records']} output_drop_bytes={m['drop_bytes']}")
        elif m['kind'] == TERM_OUT and self.connected:
            seq = m['seq']
            if seq < self.out_next:
                return
            if seq > max(self.out_next, self.state_next):
                self.diagnostic(f'output gap: {seq - max(self.out_next, self.state_next)}')
            self.out_next = seq + 1
            self.display.write(m['payload'])
            self.display.flush()
            with (h.storage(h.boot) / (h.boot.hex() + '.terminal')).open('ab') as f:
                f.write(m['payload'])

def format_response(m):
    status = m['status']
    name = STATUS_NAMES[status] if status < len(STATUS_NAMES) else str(status)
    payload, op = m['payload'], m['opcode']
    result = f"response {m['request_id']} {name}"
    if status != 0:
        return result + (f' {payload.hex()}' if payload else '')
    if op == CLOCK and len(payload) == 24:
        tick, hz, tb, us = struct.unpack('!IIQQ', payload)
        return result + f' tick={tick} tb_hz={hz} tb={tb} us={us}'
    if op in (STATUS, MEMORY) and len(payload) % (8 if op == MEMORY else 4) == 0:
        values = struct.unpack(f'!{len(payload) // 4}I', payload)
        if op == MEMORY:
            return result + ' ' + ' '.join(f'base=0x{values[i]:08x} size=0x{values[i+1]:08x}' for i in range(0, len(values), 2))
        return result + ' u32=' + ','.join(str(v) for v in values)
    return result + ' ' + payload.hex()


class InputLines:
    def __init__(self, fd):
        self.fd = fd
        self.buffer = b''
        os.set_blocking(fd, False)

    def read(self):
        chunk = os.read(self.fd, 4096)
        self.buffer += chunk
        lines = self.buffer.split(b'\n')
        self.buffer = lines.pop()
        if not chunk and self.buffer:
            lines.append(self.buffer)
            self.buffer = b''
        return chunk != b'', [line.decode('utf-8') for line in lines]



def terminal_loop(host, stdin):
    terminal = Terminal(host)
    saved = termios.tcgetattr(stdin) if os.isatty(stdin) else None
    try:
        if saved is not None:
            tty.setraw(stdin)
        opened = False
        while True:
            ready, _, _ = select.select([host.io, stdin], [], [], 0.1)
            if host.io in ready:
                frame = host.io.receive()
                if host.boot is None:
                    host.receive(frame)
                else:
                    terminal.receive(frame)
                if host.boot is not None and not opened:
                    terminal.open()
                    opened = True
            if stdin in ready:
                data = os.read(stdin, 4096)
                if not data:
                    return 0
                before, escape, _ = data.partition(b'\x1d')
                terminal.send_bytes(before)
                if escape:
                    return 0
    finally:
        if saved is not None:
            termios.tcsetattr(stdin, termios.TCSADRAIN, saved)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--udp')
    group.add_argument('--iface')
    parser.add_argument('--mac', default='02:00:00:00:00:01')
    parser.add_argument('--peer', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--boot-id')
    parser.add_argument('--terminal', action='store_true')
    args = parser.parse_args()
    try:
        peer = mac(args.peer)
        boot = bytes.fromhex(args.boot_id.replace('-', '')) if args.boot_id else None
        if boot is not None and len(boot) != 16:
            raise ValueError('boot-id must be 16 bytes')
        io = UDP(args.udp, mac(args.mac)) if args.udp else Packet(args.iface)
        host = Host(io, peer, args.out, boot, output=(lambda text: print(text, file=sys.stderr)) if args.terminal else print)
        stdin = sys.stdin.fileno()
        if args.terminal:
            return terminal_loop(host, stdin)
        input_lines = InputLines(stdin)
        while True:
            ready, _, _ = select.select([io] + ([stdin] if stdin is not None else []), [], [], 0.1)
            if io in ready:
                host.receive(io.receive())
            if stdin is not None and stdin in ready:
                alive, lines = input_lines.read()
                if not alive:
                    stdin = None
                for line in lines:
                    try:
                        host.request(line)
                    except ValueError as exc:
                        print(exc, file=sys.stderr)
            host.expire()
    except (ValueError, OSError, OverflowError) as exc:
        print(exc, file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('loss after last frame remains unknown')
    return 0


if __name__ == '__main__':
    sys.exit(main())
