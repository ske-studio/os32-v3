#!/usr/bin/env python3
"""Single-owner serial recover monitor. No LAN dependency; timeout is never success.

Use an already-running rshell. --command starts an explicitly approved trial;
without it, send recover now SESSION. Keep --state and --log across PAUSED calls.
--restore sends recover restore without saved state; during RUN it sends the existing sid abort.
--status sends only recover status (no trial, abort, boot probe or state update).
--port aidebug:http://127.0.0.1:8025 uses NP21/W COM1; do not use /api/cmd
or another serial reader concurrently. NP21/W does not emulate serial baud timing.
--image-crc is the expected deployed vmkernel image CRC, checked with ver first.
A saved session skips ver (PAUSED forbids ordinary commands); reset detection then
relies on session/sequence/metadata. --session 0 requests untested S0 recovery.
"""
import argparse
import copy
import sys
from contextlib import closing
import io
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


# Serial frame bounds and protocol enums are kept beside the parser.
STATE_VERSION = 2
MAX_LINE = 4096
DEFAULT_CLEANUP_TIMEOUT = 180
DEFAULT_TIMEOUT = 240
METADATA = ('version', 'profile_crc', 'plan_hash', 'snapshot_crc')
PAIRED = ('snapshot', 'access', 'restore', 'decode', 'screen', 'route', 'wait', 'cell')
PRE_ID = ('main', 'pc', 'opcode', 'exec', 'substep')
MAIN_TIMEOUT = 900
C_TIMEOUT = 1800
RESTORE_TIMEOUT = 180
BUS_ID = ('pc', 'bar', 'off', 'width')
DIAG_FN = ('NONE', 'READY', 'PUT', 'WAIT_FRAME', 'PIT_WAIT', 'BEGIN', 'EMPTY', 'BUDGET', 'ROW')
DIAG_WHERE = ('NONE', 'BYTE', 'BUSY_WAIT', 'AFTER_PUT', 'FLUSH', 'SEPARATOR', 'BUILD')
BOOL_FIELDS = ('exec', 'minimal', 'capacity_known', 'full_capacity', 'verified',
               'unknown', 'override', 'txd', 'sent', 'mismatch')


class Records:
    def __init__(self, session, previous=None, virtual=None, resync=False):
        self.resync = resync
        self.virtual = (previous.get('virtual', False)
                        if virtual is None and isinstance(previous, dict) else bool(virtual))
        self.data = copy.deepcopy(previous) if previous is not None else dict(
            format=STATE_VERSION, virtual=self.virtual, sid=session, seq=0, crc={}, stack=[], invalid=None,
            diag=None, final=None, paused=False, cleanup_seconds=None)
        d = self.data
        if isinstance(d, dict):
            d['virtual'] = self.virtual
        if (not isinstance(d, dict) or d.get('format') != STATE_VERSION
                or not isinstance(d.get('stack'), list) or not isinstance(d.get('crc'), dict)
                or any(k not in d for k in ('invalid', 'diag', 'final', 'paused', 'cleanup_seconds', 'sid', 'seq'))):
            raise RecoverError('invalid/old saved state')
        if (any(type(d[k]) is not int or not 0 <= d[k] <= 4294967295 for k in ('sid', 'seq'))
                or type(d['paused']) is not bool
                or (d['invalid'] is not None and not isinstance(d['invalid'], str))
                or any(k not in METADATA or type(v) is not int or not 0 <= v <= 4294967295
                       for k, v in d['crc'].items())
                or (d['cleanup_seconds'] is not None and
                    (type(d['cleanup_seconds']) is not int or not 0 <= d['cleanup_seconds'] <= 4294967295))):
            raise RecoverError('invalid saved state')
        if d['sid'] != session:
            raise RecoverError('saved session differs')
        if d['invalid']:
            raise RecoverError(d['invalid'])
        for fields in d['stack'] + [f for f in (d['diag'], d['final']) if f is not None]:
            if not isinstance(fields, dict) or any(not isinstance(k, str) or not isinstance(v, str)
                                                  for k, v in fields.items()):
                raise RecoverError('invalid saved record')
            self._fields('REC ' + ' '.join(k + '=' + v for k, v in fields.items()))
            if int(fields['sid']) != session or int(fields['seq']) > d['seq']:
                raise RecoverError('invalid saved record sequence')
        if any(f['event'] != 'BEGIN' or f['phase'] not in PAIRED + ('gf_pre', 'gf_bus')
               for f in d['stack']):
            raise RecoverError('invalid saved stack')
        if ((d['diag'] is not None and d['diag']['phase'] != 'diag')
                or (d['final'] is not None and d['final']['phase'] != 'final')
                or ((d['paused'] or d['final'] is not None) and d['stack'])):
            raise RecoverError('invalid saved terminal state')
        self.final = d['final']
        self.paused = d['paused']

    def invalidate(self, message):
        if not self.data['invalid']:
            self.data['invalid'] = message
        return self.data['invalid']

    def _fields(self, line):
        fields = {}
        def malformed(field):
            return RecoverError('malformed REC sid=%s seq=%s field=%s' %
                                (fields.get('sid', '?'), fields.get('seq', '?'), field))
        if line.endswith('\n'):
            line = line[:-1]
        if line.endswith('\r'):
            line = line[:-1]
        if not line.isascii() or '\r' in line:
            raise malformed('ascii')
        for part in line.split(' ')[1:]:
            if part.count('=') != 1:
                raise malformed('token')
            key, value = part.split('=')
            if not key or not value or key in fields:
                raise malformed(key or 'token')
            fields[key] = value
        for name in ('sid', 'seq', 'phase', 'event'):
            if name not in fields:
                raise malformed(name)
        phase, event = fields['phase'], fields['event']
        required = ()
        if phase == 'state' and event in ('IDLE', 'SNAPSHOTTING', 'ARMED', 'RUNNING', 'PAUSED', 'RESTORING'):
            required = METADATA + (('cleanup_seconds',) if event == 'RESTORING' else ())
        elif phase in PAIRED and event in ('BEGIN', 'END') and not (phase == 'route' and event == 'END' and 'source' in fields):
            required = ('step', 'addr') if event == 'BEGIN' else ('rc', 'elapsed')
        elif phase == 'gf_pre' and event in ('BEGIN', 'END'):
            required = PRE_ID + (('rc',) if event == 'END' else ())
        elif phase == 'gf_bus' and event in ('BEGIN', 'END'):
            if 'len' in fields:
                required = ('pc', 'bar', 'off', 'len', 'dir')
                if 'row' in fields:
                    required += ('row',)
                if event == 'END':
                    required += ('rc',) if 'row' in fields else ('bad_off', 'expected', 'actual', 'rc')
            else:
                required = BUS_ID + (('old', 'new') if event == 'BEGIN' else ('readback', 'rc'))
                if event == 'END' and 'mismatch' in fields:
                    required += ('mismatch', 'space', 'port', 'index')
                    if fields['mismatch'] != '1' or fields.get('rc') != '0':
                        raise malformed('mismatch')
        elif phase == 'check' and event == 'END' or phase == 'route' and event == 'END' and 'source' in fields:
            required = ('source', 'key', 'mask', 'expected', 'actual', 'rc')
        elif phase == 'gf_verify' and event == 'END':
            required = ('saved_words', 'minimal', 'capacity_known', 'full_capacity', 'capacity_bytes', 'tested_bytes')
        elif phase == 'receive' and event == 'DISCARD':
            required = ('discarded_bytes',)
        elif phase == 'diag' and event == 'TX':
            required = ('fail_seq', 'pos', 'fn', 'rc', 'where', 'sent', 'polls', 'bytes')
        elif phase == 'final' and event == 'END':
            required = ('result', 'target', 'verified', 'unknown', 'override', 'saved', 'actual', 'trial', 'lost_lines', 'discarded_bytes', 'txd')
        elif phase == 'inject' and event == 'BEGIN' and self.virtual:
            required = ('step', 'addr')
        else:
            raise malformed('phase/event')
        allowed = set(('sid', 'seq', 'phase', 'event') + required)
        for name in required:
            if name not in fields:  # Mandatory fields must precede state updates.
                raise malformed(name)
        for name, value in fields.items():
            if name not in allowed:
                raise malformed(name)
            enum = {'fn': DIAG_FN, 'where': DIAG_WHERE,
                    'result': ('OK', 'FAIL', 'PARTIAL'), 'target': ('S0',)}.get(name)
            if enum is not None:
                if value not in enum:
                    raise malformed(name)
            elif name not in ('phase', 'event'):
                if not re.fullmatch(r'-?[0-9]+' if name == 'rc' else r'[0-9]+', value):
                    raise malformed(name)
                low, high = (-2147483648, 2147483647) if name == 'rc' else (0, 4294967295)
                if name in BOOL_FIELDS:
                    high = 1
                if name == 'trial':
                    high = 9
                if not low <= int(value) <= high:
                    raise malformed(name)
        return fields

    def line(self, line):
        if not line.startswith('REC '):
            return
        if self.data['invalid']:
            raise RecoverError(self.data['invalid'])
        try:
            fields = self._fields(line)
            sid, seq = int(fields['sid']), int(fields['seq'])
            candidate_sid = self.data['sid']
            if candidate_sid == 0 and self.data['seq'] == 0 and sid > 0:
                candidate_sid = sid
            if self.resync and self.data['seq'] == 0:
                self.data['seq'] = seq - 1
                self.resync = False
            if sid != candidate_sid or seq != self.data['seq'] + 1:
                raise RecoverError('session/sequence mismatch or missing REC')
            if self.final is not None:
                raise RecoverError('REC after final')
            phase, event = fields['phase'], fields['event']
            stack = copy.deepcopy(self.data['stack'])
            paired = phase in PAIRED or phase in ('gf_pre', 'gf_bus')
            if paired and event == 'BEGIN':
                stack.append(fields)
            elif paired and event == 'END':
                ids = (PRE_ID if phase == 'gf_pre' else
                       (('pc', 'bar', 'off', 'len', 'dir') + (('row',) if 'row' in fields else ())
                        if 'len' in fields else BUS_ID) if phase == 'gf_bus' else ())
                if (not stack or stack[-1]['phase'] != phase
                        or any(stack[-1][key] != fields[key] for key in ids)):
                    raise RecoverError('unmatched BEGIN/END sid=%s seq=%s phase=%s' % (sid, seq, phase))
                stack.pop()
            if (event == 'PAUSED' or phase == 'final') and stack:
                raise RecoverError('unmatched BEGIN/END sid=%s seq=%s phase=%s' % (sid, seq, stack[-1]['phase']))
            crc = dict(self.data['crc'])
            for name in METADATA:
                if name in fields:
                    value = int(fields[name])
                    if self.data.get('metadata_frozen') and name in crc and crc[name] != value:
                        raise RecoverError('session metadata changed')
                    crc[name] = value
            if phase == 'diag' and self.data['diag'] is not None:
                raise RecoverError('duplicate diagnostic')
            self.data.update(sid=candidate_sid, seq=seq, stack=stack, crc=crc)
            self.paused = event == 'PAUSED'
            self.data['paused'] = self.paused
            if self.paused:
                self.data['metadata_frozen'] = True
            if event == 'RESTORING':
                if 'cleanup_seconds' not in fields:
                    raise RecoverError('RESTORING without cleanup_seconds')
                self.data['cleanup_seconds'] = int(fields['cleanup_seconds'])
            if phase == 'diag':
                self.data['diag'] = fields
            if phase == 'final':
                self.final = fields
                self.data['final'] = fields
        except RecoverError as exc:
            raise RecoverError(self.invalidate(str(exc))) from exc

    def success(self):
        f = self.final or {}
        return (not self.data['invalid'] and not self.data['stack']
                and self.data['diag'] is not None and self.data['diag']['fn'] == 'NONE'
                and f.get('txd') == '0'
                and f.get('result') in ('OK', 'PARTIAL') and f.get('target') == 'S0'
                and f.get('verified') == '1' and f.get('lost_lines') == '0'
                and all(k in self.data['crc'] for k in METADATA))


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
             clock=time.monotonic, initial=b''):
    """Read in finite chunks; record errors still drain cleanup until its deadline."""
    start = clock()
    if command is not None:
        write_bounded(port, (command + '\n').encode('ascii'), start + timeout, clock)
    deadline = start + timeout
    aborted = command is None
    failure = None
    echo = command is None
    line = bytearray()
    dropping = False
    c_command = bool(command and re.match(r'gfinit\s+(modeset|c-only)\b', command))
    c_result = None
    def record_failure(message):
        nonlocal failure
        failure = failure or message
        if records is not None:
            records.invalidate(failure)
            save(records.data)
    while True:
        if clock() >= deadline:
            if line or dropping:
                record_failure('truncated line before EOT/deadline')
            if records is None or aborted:
                raise RecoverError(failure or 'terminal/EOT missing after deadline')
            aborted = True
            record_failure('host timeout')
            deadline = clock() + cleanup_timeout
            try:
                write_bounded(port, abort_frame(records.data['sid']), deadline, clock)
            except RecoverError as exc:
                record_failure(str(exc))
        chunk = initial or port.read(256)
        initial = b''
        if chunk:
            raw.write(chunk)
            raw.flush()
        for byte in chunk:
            if byte == 4:
                if not echo:
                    continue  # Initial rshell EOT is not this command's reply.
                if line or dropping:
                    record_failure('truncated line before EOT/deadline')
                if failure:
                    raise RecoverError(failure)
                if records is None:
                    return
                if c_command and c_result is None:
                    raise RecoverError('missing/failed guest C result')
                if records.success():
                    return records.final['result']
                if records.paused and records.final is None and not records.data['stack']:
                    if len(records.data['crc']) == 4:
                        if c_command:
                            if c_result is None or c_result[2] != 'OK':
                                raise RecoverError('missing/failed guest C result')
                            print('MODE_C: ' + ('観測完了' if c_result[1] == 'record' else '機械項目合格・映像保留'))
                        return 'PAUSED'
                raise RecoverError('missing/failed verification or lost logs')
            if byte == 10:
                text = line.decode('ascii', errors='replace')
                line.clear()
                if dropping:
                    dropping = False
                    continue
                if command is not None and text in ('> ' + command, '> ' + command + '\r'):
                    echo = True
                if echo:
                    match = re.fullmatch(r'GF C verify=(record|required) result=(OK|NG)\r?', text)
                    if match:
                        c_result = match.group(0), match.group(1), match.group(2)
                        print(text.rstrip('\r'))
                        if match.group(2) == 'NG':
                            record_failure('guest C failed')
                    if text.startswith(('recover: rejected', 'gfinit: rejected rc=')):
                        record_failure('guest rejected recovery')
                    try:
                        if records is not None:
                            records.line(text)
                            save(records.data)
                    except RecoverError as exc:
                        record_failure(str(exc))
                continue
            if not dropping:
                line.append(byte)
                if len(line) > MAX_LINE:
                    record_failure('overlong serial line')
                    line.clear()
                    dropping = True


def restore_exchange(port, timeout, cleanup_timeout, raw, clock=time.monotonic):
    # A live stream supplies its sid. Never reenter the shell during RUN.
    deadline = clock() + timeout
    line = bytearray()
    dropping = False
    while clock() < deadline:
        chunk = port.read(256)
        for pos, byte in enumerate(chunk):
            if byte == 4:
                # EOT closes the current command: the shell can accept restore.
                raw.write(line + b'\x04')
                raw.flush()
                return exchange(port, 'recover restore', Records(0, resync=True),
                                timeout, cleanup_timeout, raw, lambda _: None,
                                clock, chunk[pos+1:])
            if byte == 10:
                row = bytes(line) + b'\n'
                line.clear()
                if dropping:
                    dropping = False
                    raw.write(b'\n')
                    continue
                text = row.decode('ascii', errors='replace')
                fields = None
                try:
                    if text.startswith('REC '):
                        fields = Records(0)._fields(text)
                except RecoverError:
                    pass  # Attachment may start in the middle of a row.
                if (fields is None or not int(fields['sid']) or
                    fields['phase'] == 'final' or
                    fields['event'] in ('IDLE', 'PAUSED')):
                    raw.write(row)
                    raw.flush()
                    continue
                sid = int(fields['sid'])
                # PAUSED/final rows still precede the shell's EOT. All other
                # complete REC rows identify an active recovery session.
                write_bounded(port, abort_frame(sid), deadline, clock)
                return exchange(port, None, Records(sid, resync=True),
                                timeout, cleanup_timeout, raw, lambda _: None,
                                clock, row + chunk[pos+1:])
            elif dropping:
                raw.write(bytes((byte,)))
            else:
                line.append(byte)
                if len(line) > MAX_LINE:
                    raw.write(line)
                    line.clear()
                    dropping = True
    raw.write(line)
    raw.flush()
    raise RecoverError('restore state unknown after deadline; physical recovery required')


def verify_boot(port, expected_crc, timeout, raw, clock=time.monotonic):
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


def status(port, timeout, raw, clock=time.monotonic):
    capture = io.BytesIO()
    try:
        exchange(port, 'recover status', None, timeout, timeout, capture,
                 lambda _: None, clock)
    finally:
        raw.write(capture.getvalue())
        raw.flush()
    lines = capture.getvalue().decode('ascii', errors='replace').replace('\x04', '\n').splitlines()
    try:
        echo = lines.index('> recover status')
    except ValueError as exc:
        raise RecoverError('missing status echo') from exc
    pattern = (r'recover sid=\d+ state=\d+ reason=\d+ verified=\d+ '
               r'profile=[0-9A-Fa-f]{8} plan=[0-9A-Fa-f]{8} '
               r'snapshot=[0-9A-Fa-f]{8} seq=\d+ lost=\d+ discarded=\d+')
    replies = [line for line in lines[echo + 1:] if re.fullmatch(pattern, line)]
    if len(replies) != 1:
        raise RecoverError('missing/malformed status reply')
    diag_pattern = (r'txd sid=[0-9]+ valid=[01] seq=[0-9]+ pos=[0-9]+ fn=('
                    + '|'.join(DIAG_FN) + r') rc=-?[0-9]+ where=('
                    + '|'.join(DIAG_WHERE) + r') sent=[01] polls=[0-9]+ bytes=[0-9]+')
    diagnostics = [line for line in lines[echo + 1:] if re.fullmatch(diag_pattern, line)]
    if len(diagnostics) != 1:
        raise RecoverError('missing/malformed status diagnostic')
    return replies[0] + '\n' + diagnostics[0]


def open_port(name, baud):
    if name.startswith('aidebug:'):
        from rshell_serial import AidebugPort
        return AidebugPort(name[len('aidebug:'):], timeout=.1)
    import serial
    return serial.Serial(name, baud, timeout=.1, write_timeout=.2, exclusive=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyUSB0')
    parser.add_argument('--baud', type=int, default=9600)
    parser.add_argument('--session', type=int)
    parser.add_argument('--image-crc')
    parser.add_argument('--state', type=Path)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--timeout', type=float)
    parser.add_argument('--cleanup-timeout', type=float, default=DEFAULT_CLEANUP_TIMEOUT)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--command')
    mode.add_argument('--status', action='store_true')
    mode.add_argument('--restore', action='store_true')
    args = parser.parse_args()
    command = 'recover restore' if args.restore else 'recover status' if args.status else (args.command or 'recover now %s' % args.session)
    if args.timeout is None:
        words = command.split()
        args.timeout = (RESTORE_TIMEOUT if args.restore or words[:2] in (['gfinit', 'abort'], ['recover', 'now']) else
                        C_TIMEOUT if words[:2] in (['gfinit', 'modeset'], ['gfinit', 'c-only']) else
                        MAIN_TIMEOUT if words[:2] == ['gfinit', 'run-one'] else DEFAULT_TIMEOUT)
    if command.split()[:2] == ['gfinit', 'c-only']:
        args.session = 0
    # Restore sends before reading any saved state or checking an image CRC.
    if args.restore:
        try:
            with closing(open_port(args.port, args.baud)) as port:
                try:
                    raw = args.log.open('ab')
                except OSError:
                    raw = io.BytesIO()
                with raw:
                    print(restore_exchange(port, args.timeout, args.cleanup_timeout, raw))
            return 0
        except (RecoverError, OSError, ValueError, TypeError, KeyError) as exc:
            print('recover: ' + str(exc))
            return 1
    if args.baud <= 0 or min(args.timeout, args.cleanup_timeout) <= 0 or (
            args.session is not None and not 0 <= args.session <= 0xffffffff):
        parser.error('session must fit in u32 and deadlines must be positive')
    if not args.status and (args.session is None or args.state is None or args.image_crc is None):
        parser.error('trial/recovery needs --session, --state and --image-crc')
    if args.image_crc is not None and not re.fullmatch('[0-9a-fA-F]{8}', args.image_crc):
        parser.error('--image-crc must have 8 hexadecimal digits')
    if any(c in command for c in '\r\n\x04'):
        parser.error('one command only')
    def save(data):
        args.state.write_text(json.dumps(data, sort_keys=True) + '\n')
    try:
        previous = (json.loads(args.state.read_text())
                    if not args.status and args.state.exists() else None)
        if command.split()[:2] == ['gfinit', 'c-only']:
            previous = None
        records = None if args.status else Records(
            args.session, previous,
            virtual=command.startswith('recover virtual ') if args.command else None)
        with closing(open_port(args.port, args.baud)) as port, args.log.open('ab') as raw:
            if args.status:
                print(status(port, args.timeout, raw))
                return 0
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
    except (RecoverError, OSError, ValueError, TypeError, KeyError) as exc:
        print('recover: ' + str(exc))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
