#!/usr/bin/env python3
"""Fake serial/time tests of the actual recover host; --mutate kills guards."""
import argparse
from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
from unittest.mock import patch
import tempfile
import zlib
import mutpar

SRC = Path(__file__).resolve().parents[1] / 'rshell_recover.py'


def load(path):
    spec = importlib.util.spec_from_file_location('recover_host', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Port:
    def __init__(self, data, split=256, stalled=False, idle_reads=0):
        self.data = bytearray(data)
        self.split = split
        self.writes = []
        self.t = 0
        self.stalled = stalled
        self.idle_reads = idle_reads
    def clock(self):
        self.t += .001
        return self.t
    def read(self, count):
        self.t += .01
        if self.idle_reads:
            self.idle_reads -= 1
            return b''
        result = bytes(self.data[:min(count, self.split)])
        del self.data[:len(result)]
        return result
    def write(self, data):
        self.writes.append(data)
        return 0 if self.stalled else len(data)


META = 'REC sid=7 seq=1 phase=state event=PAUSED version=1 profile_crc=11 plan_hash=12 snapshot_crc=13\n'
FINAL = 'REC sid=7 seq=2 phase=final event=END result=OK target=S0 verified=1 unknown=0 override=0 trial=0 lost_lines=0 discarded_bytes=5\n'
ECHO = '> recover now 7\n'


def cases(m):
    fresh = m.Records(0)
    fresh.line(META)
    assert fresh.data['sid'] == 7
    head = b'\xa5\x5a' + struct.pack('<I', 7)
    assert m.abort_frame(7) == head + struct.pack('<I', zlib.crc32(head))
    def run(data, good=False, **kw):
        port = Port(data.encode(), **kw)
        raw = io.BytesIO()
        saves = []
        records = m.Records(7)
        try:
            result = m.exchange(port, 'recover now 7', records, 10, 1, raw,
                                lambda data: saves.append(dict(data)), port.clock)
        except m.RecoverError:
            assert not good
            return port
        assert good, result
        assert saves[-1]['crc']['profile_crc'] == 11
        return port
    for split in (1, 7, 256):
        run('\x04'+ECHO+META+FINAL+'\x04', good=True, split=split)
    run(ECHO+META+'\x04', good=True)
    for old, new in [('sid=7','sid=6'), ('seq=2','seq=3'), ('lost_lines=0','lost_lines=1'),
                     ('verified=1','verified=0'), ('result=OK','result=FAIL'),
                     ('unknown=0','unknown=1'), ('override=0','override=1')]:
        run(ECHO+META+FINAL.replace(old,new)+'\x04')
    run(ECHO+META+FINAL+'recover: rejected rc=-1\n\x04')
    run(ECHO+FINAL+'\x04')
    run(ECHO+'\x04')
    run(META+FINAL+'\x04')
    p = run(ECHO+META+FINAL)  # final without EOT cannot be success
    assert p.writes.count(m.abort_frame(7)) == 1
    run(ECHO+META+FINAL+'\x04', stalled=True)
    p = run(ECHO)  # timeout alone never proves restoration
    assert p.writes.count(m.abort_frame(7)) == 1 and p.t < 12
    run(ECHO+META+FINAL+'\x04', idle_reads=920)
    run(ECHO+META+META+'\x04')
    port = Port(b'> ver\n  Build: Oct 21 2026\n  Image CRC: 1234abcd (100 bytes, HDD loader)\n\x04', split=1)
    m.verify_boot(port, '1234abcd', 10, io.BytesIO(), port.clock)
    for data in (b'> ver\n  Build: today\n\x04', b'> other\n\x04', b'> ver\n  Image CRC: 1234abcd (100 bytes, HDD loader)\n\x04'):
        port = Port(data)
        try:
            m.verify_boot(port, '1234abcd', 1, io.BytesIO(), port.clock)
        except m.RecoverError:
            pass
        else:
            raise AssertionError('unverified boot accepted')


def status_cases(m):
    reply = ('recover sid=0 state=0 reason=0 verified=0 profile=00000000 '
             'plan=00000000 snapshot=00000000 seq=0 lost=0 discarded=0')
    data = ('\x04> recover status\n' + reply + '\n\x04').encode()
    for split in (1, 7, 256):
        port = Port(data, split=split)
        raw = io.BytesIO()
        assert m.status(port, 10, raw, port.clock) == reply
        assert raw.getvalue() == data
        assert port.writes == [b'recover status\n']
    for data in (b'> recover status\n\x04', b'> recover status\nrecover: rejected rc=-1\n\x04',
                 b'> recover status\nrecover: kdebug unavailable\n\x04',
                 ('> recover status\n' + reply + '\n').encode(),
                 (reply + '\n\x04').encode()):
        port = Port(data)
        try:
            m.status(port, .2, io.BytesIO(), port.clock)
        except m.RecoverError:
            pass
        else:
            raise AssertionError('invalid status accepted')
        assert port.writes == [b'recover status\n']  # Never abort an observational call.

    calls = []
    fake = SimpleNamespace(
        AidebugPort=lambda *a, **kw: calls.append(('aidebug', a, kw)),
        Serial=lambda *a, **kw: calls.append(('serial', a, kw)))
    with patch.dict(sys.modules, {'rshell_serial': fake, 'serial': fake}):
        m.open_port('aidebug:http://localhost:8025', 9600)
        m.open_port('/dev/fake', 19200)
    assert calls == [('aidebug', ('http://localhost:8025',), {'timeout': .1}),
                     ('serial', ('/dev/fake', 19200),
                      {'timeout': .1, 'write_timeout': .2, 'exclusive': True})]
    # CLI status needs no boot/session files and sends no ver or recovery command.
    with tempfile.TemporaryDirectory(prefix='recover_status_') as tmp:
        port = Port(('> recover status\n' + reply + '\n\x04').encode())
        closed = []
        port.close = lambda: closed.append(True)
        log = Path(tmp) / 'raw.log'
        state = Path(tmp) / 'state.json'
        state.write_text('preserve even non-JSON data')
        argv = ['rshell_recover', '--status', '--log', str(log), '--state', str(state)]
        with patch.object(sys, 'argv', argv), patch.object(m, 'open_port', return_value=port), redirect_stdout(io.StringIO()) as out:
            assert m.main() == 0
        assert out.getvalue().strip() == reply
        assert state.read_text() == 'preserve even non-JSON data'
        assert port.writes == [b'recover status\n'] and closed == [True]
        assert log.read_bytes().endswith(b'\x04')


def gfinit_cases(m):
    meta = META.replace('sid=7', 'sid=6')
    final = FINAL.replace('sid=7', 'sid=6')
    command = 'gfinit prepare 5 --allow-write'
    for split in (1, 7, 256):
        for ending, good in [
                (meta, True), (meta+final, True), ('', False),
                ('gfinit: rejected rc=-1\n', False),
                (meta+'gfinit: rejected rc=-1\n', False),
                (meta+final+'gfinit: rejected rc=-1\n', False),
                (meta+final.replace('result=OK', 'result=PARTIAL'), False),
                (meta+final.replace('override=0', 'override=1'), False)]:
            data = ('> '+command+'\n'+ending+'\x04').encode()
            port = Port(data, split=split)
            raw = io.BytesIO(); saves = []; records = m.Records(0)
            try:
                m.exchange(port, command, records, 10, 1, raw,
                           lambda d: saves.append(dict(d)), port.clock)
            except m.RecoverError:
                assert not good
            else:
                assert good
            assert raw.getvalue() == data
            assert port.writes == [(command+'\n').encode()]
            if ending.startswith(meta):
                assert saves[-1]['sid'] == 6 and saves[-1]['crc']['snapshot_crc'] == 13
    # A2 PAUSED saves the newly assigned sid; continuation never probes ver.
    records = m.Records(0);records.line(meta)
    resumed = m.Records(6, records.data)
    command = 'gfinit run-one 6 0 --allow-write'
    port = Port(('> '+command+'\n').encode())
    try:
        m.exchange(port, command, resumed, 1, 1, io.BytesIO(), lambda _: None, port.clock)
    except m.RecoverError:
        pass
    else:
        raise AssertionError('timeout accepted')
    assert port.writes == [(command+'\n').encode(), m.abort_frame(6)]
    try:
        m.Records(5, records.data)
    except m.RecoverError:
        pass
    else:
        raise AssertionError('stale session accepted')


MUTATIONS = [
    ('session', "sid != self.data['sid'] or seq != self.data['seq'] + 1", "seq != self.data['seq'] + 1"),
    ('sequence', "sid != self.data['sid'] or seq != self.data['seq'] + 1", "sid != self.data['sid']"),
    ('loss', "f.get('lost_lines') == '0'", 'True'),
    ('verify', "f.get('verified') == '1'", 'True'),
    ('echo', 'if not echo:', 'if False:'),
    ('status-body', "raise RecoverError('missing/malformed status reply')", "return 'accepted'"),
    ('serial-owner', 'exclusive=True', 'exclusive=False'),
    ('timeout', "failure = failure or 'host timeout'", 'failure = failure'),
    ('gfinit-rejected', "'gfinit: rejected rc='", "'gfinit: ignored rc='"),
]


def mutation(item):
    name, before, after = item
    source = SRC.read_text()
    if before and source.count(before) != 1:
        return name + ': ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='recover_host_') as tmp:
        path = Path(tmp) / (name + '.py')
        path.write_text(source.replace(before, after, 1) if before else source)
        mutant = load(path)
        try:
            cases(mutant)
            status_cases(mutant)
            gfinit_cases(mutant)
        except (AssertionError, mutant.RecoverError):
            return name + ': RED', 0
        return name + ': GREEN', 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cases(load(SRC))
    status_cases(load(SRC))
    gfinit_cases(load(SRC))
    print('recover host: PASS')
    if args.mutate:
        # Serial: status_cases patches sys.modules/argv/stdout, which threads would share.
        missed = 0
        for item in [('control', '', '')] + MUTATIONS:
            text, survived = mutation(item)
            if item[0] == 'control':
                survived = not survived
            print(('CONTROL ' if item[0] == 'control' else '') + text)
            missed += survived
        return int(bool(missed))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
