#!/usr/bin/env python3
"""Single-owner serial recover monitor. No LAN dependency; timeout is never success.

Use an already-running rshell. --command starts an explicitly approved trial;
without it, send recover now SESSION. Keep --state and --log across PAUSED calls.
--image-crc is the expected deployed vmkernel image CRC, checked with ver first.
A saved session skips ver (PAUSED forbids ordinary commands); reset detection then
relies on session/sequence/metadata. --session 0 requests untested S0 recovery.
"""
import argparse
import fcntl
import json
from pathlib import Path
import re
import struct
import time
import zlib


def abort_frame(session):
    head = b'\xa5\x5a' + struct.pack('<I', session)
    return head + struct.pack('<I', zlib.crc32(head))


class RecoverError(Exception):
    pass


class Records:
    def __init__(self, session, previous=None):
        self.data = previous or dict(sid=session, seq=0, crc={}, last_begin=None)
        if self.data['sid'] != session:
            raise RecoverError('saved session differs')
        self.final = None
        self.paused = False

    def line(self, line):
        if not line.startswith('REC '):
            return
        fields = dict(part.split('=', 1) for part in line.split()[1:] if '=' in part)
        try:
            sid, seq = int(fields['sid']), int(fields['seq'])
            if self.data['sid'] == 0 and self.data['seq'] == 0 and sid > 0:
                self.data['sid'] = sid
            if sid != self.data['sid'] or seq != self.data['seq'] + 1:
                raise RecoverError('session/sequence mismatch or missing REC')
            self.data['seq'] = seq
            for name in ('version', 'profile_crc', 'plan_hash', 'snapshot_crc'):
                if name in fields:
                    value = int(fields[name])
                    if self.data.get('metadata_frozen') and name in self.data['crc'] and self.data['crc'][name] != value:
                        raise RecoverError('session metadata changed')
                    self.data['crc'][name] = value
            if fields.get('event') == 'BEGIN':
                self.data['last_begin'] = fields
            self.paused = fields.get('event') == 'PAUSED'
            if self.paused:
                self.data['metadata_frozen'] = True
            if fields.get('phase') == 'final' and fields.get('event') == 'END':
                self.final = fields
        except (ValueError, KeyError) as exc:
            raise RecoverError('malformed REC') from exc

    def success(self):
        f = self.final or {}
        return (f.get('result') == 'OK' and f.get('target') == 'S0'
                and f.get('verified') == '1' and f.get('unknown') == '0'
                and f.get('override') == '0' and f.get('lost_lines') == '0'
                and all(k in self.data['crc'] for k in
                        ('version', 'profile_crc', 'plan_hash', 'snapshot_crc')))


def write_bounded(port, data, deadline, clock):
    while data:
        if clock() >= deadline:
            raise RecoverError('serial write deadline')
        try:
            count = port.write(data)
        except OSError as exc:
            raise RecoverError('serial write failed') from exc
        if not count:
            raise RecoverError('serial write stalled')
        data = data[count:]


def exchange(port, command, records, timeout, cleanup_timeout, raw, save,
             clock=time.monotonic):
    """Read in finite chunks; record errors still drain cleanup until its deadline."""
    start = clock()
    write_bounded(port, (command + '\n').encode('ascii'), start + timeout, clock)
    deadline = start + timeout
    aborted = False
    failure = None
    echo = False
    line = bytearray()
    while True:
        if clock() >= deadline:
            if records is None or aborted:
                raise RecoverError(failure or 'terminal/EOT missing after deadline')
            aborted = True
            failure = failure or 'host timeout'
            deadline = clock() + cleanup_timeout
            try:
                write_bounded(port, abort_frame(records.data['sid']), deadline, clock)
            except RecoverError as exc:
                failure = str(exc)
        chunk = port.read(256)
        if chunk:
            raw.write(chunk)
            raw.flush()
        for byte in chunk:
            if byte == 4:
                if not echo:
                    continue  # Initial rshell EOT is not this command's reply.
                if failure:
                    raise RecoverError(failure)
                if records is None:
                    return
                if records.success():
                    return 'OK'
                if records.paused and records.final is None:
                    if len(records.data['crc']) == 4:
                        return 'PAUSED'
                raise RecoverError('missing/failed verification or lost logs')
            if byte == 10:
                text = line.decode('ascii', errors='replace').rstrip('\r')
                line.clear()
                if text == '> ' + command:
                    echo = True
                if echo:
                    if text.startswith('recover: rejected'):
                        failure = 'guest rejected recovery'
                    try:
                        if records is not None:
                            records.line(text)
                            save(records.data)
                    except RecoverError as exc:
                        failure = str(exc)
                continue
            line.append(byte)
            if len(line) > 4096:
                raise RecoverError('overlong serial line')


def verify_boot(port, expected_crc, timeout, raw, clock=time.monotonic):
    import io
    capture = io.BytesIO()
    try:
        exchange(port, 'ver', None, timeout, timeout, capture, lambda _: None, clock)
    finally:
        raw.write(capture.getvalue())
    lines = capture.getvalue().decode('ascii', errors='replace').splitlines()
    pattern = r'  Image CRC: ' + expected_crc + r' \([0-9]+ bytes, (HDD|FD) loader\)'
    if not (any(re.fullmatch(pattern, x, re.IGNORECASE) for x in lines)
            and any(x.startswith('  Build: ') for x in lines)):
        raise RecoverError('boot image CRC/build verification failed')


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyUSB0')
    parser.add_argument('--baud', type=int, default=9600)
    parser.add_argument('--session', type=int, required=True)
    parser.add_argument('--image-crc', required=True)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--cleanup-timeout', type=float, default=40)
    parser.add_argument('--command')
    args = parser.parse_args()
    if not 0 <= args.session <= 0xffffffff or min(args.timeout, args.cleanup_timeout) <= 0:
        parser.error('session and deadlines must be positive')
    if not re.fullmatch('[0-9a-fA-F]{8}', args.image_crc):
        parser.error('--image-crc must have 8 hexadecimal digits')
    command = args.command or 'recover now %d' % args.session
    if any(c in command for c in '\r\n\x04'):
        parser.error('one command only')
    previous = json.loads(args.state.read_text()) if args.state.exists() else None
    records = Records(args.session, previous)
    def save(data):
        args.state.write_text(json.dumps(data, sort_keys=True) + '\n')
    try:
        with serial.Serial(args.port, args.baud, timeout=.1, write_timeout=.2,
                           exclusive=True) as port, args.log.open('ab') as raw:
            fcntl.flock(port.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            if previous is None:
                verify_boot(port, args.image_crc, args.timeout, raw)
                records.data['image_crc'] = args.image_crc.lower()
            elif previous.get('image_crc') != args.image_crc.lower():
                raise RecoverError('saved boot CRC differs')
            save(records.data)
            result = exchange(port, command, records, args.timeout,
                              args.cleanup_timeout, raw, save)
            print(result)
            return 0
    except (RecoverError, OSError, serial.SerialException) as exc:
        print('recover: ' + str(exc))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
