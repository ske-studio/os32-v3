#!/usr/bin/env python3
"""Fake serial/time tests of the actual recover host; --mutate kills guards."""
import argparse
from contextlib import redirect_stdout, redirect_stderr
import importlib.util
import io
import json
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
DIAG = 'REC sid=7 seq=2 phase=diag event=TX fail_seq=0 pos=0 fn=NONE rc=0 where=NONE sent=0 polls=0 bytes=0\n'
FINAL = DIAG + 'REC sid=7 seq=3 phase=final event=END result=OK target=S0 verified=1 unknown=0 override=0 saved=0 actual=0 trial=0 lost_lines=0 discarded_bytes=5 txd=0\n'
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
    for old, new in [('sid=7','sid=6'), ('seq=3','seq=4'), ('lost_lines=0','lost_lines=1'),
                     ('verified=1','verified=0'), ('result=OK','result=FAIL'),
                     ('unknown=0','unknown=1'), ('override=0','override=1'),
                     ('txd=0','txd=1'), ('trial=0','trial=1'),
                     ('result=OK','result=PARTIAL'), ('fn=NONE','fn=PUT')]:
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
             'plan=00000000 snapshot=00000000 seq=0 lost=0 discarded=0\n'
             'txd sid=0 valid=0 seq=0 pos=0 fn=NONE rc=0 where=NONE sent=0 polls=0 bytes=0')
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


def protocol_cases(m):
    def row(seq, phase, event, extra=''):
        return 'REC sid=7 seq=%d phase=%s event=%s%s\n' % (seq, phase, event, ' '+extra if extra else '')
    def fields(seq, phase, event, extra=''):
        return row(seq, phase, event, extra).rstrip('\n')
    meta = 'version=1 profile_crc=11 plan_hash=12 snapshot_crc=13'
    diag = 'fail_seq=0 pos=0 fn=NONE rc=0 where=NONE sent=0 polls=0 bytes=0'
    final = 'result=OK target=S0 verified=1 unknown=0 override=0 saved=0 actual=0 trial=0 lost_lines=0 discarded_bytes=0 txd=0'
    # phase-based LIFO accepts same-phase recursion and bus/access nesting.
    nested = [('gf_pre', 'BEGIN', 'main=0 pc=1 opcode=2 exec=1 substep=3'),
              ('gf_bus', 'BEGIN', 'pc=1 bar=0 off=2 width=4 old=0 new=1'),
              ('access', 'BEGIN', 'step=0 addr=2'), ('access', 'END', 'rc=0 elapsed=0'),
              ('gf_bus', 'END', 'pc=1 bar=0 off=2 width=4 readback=1 rc=0'),
              ('gf_pre', 'END', 'main=0 pc=1 opcode=2 exec=1 substep=3 rc=0'),
              ('screen', 'BEGIN', 'step=0 addr=0'), ('screen', 'BEGIN', 'step=1 addr=0'),
              ('screen', 'END', 'rc=0 elapsed=0'), ('screen', 'END', 'rc=0 elapsed=0')]
    r = m.Records(7)
    for seq, args in enumerate(nested, 1):
        r.line(fields(seq, *args))
    assert r.data['stack'] == []
    # All structurally invalid records preserve both the first error and seq.
    invalid = [
        fields(1, 'state', 'PAUSE', meta), fields(1, 'state', 'PAUSED', meta+' x=0'),
        fields(1, 'state', 'PAUSED', meta+' sid=7'), fields(1, 'state', 'PAUSED', meta+' bare'),
        fields(1, 'state', 'PAUSED', meta.replace('profile_crc=11', '')),
        fields(1, 'state', 'PAUSED', meta.replace('profile_crc=11', 'profile_crc=4294967296')),
        fields(1, 'state', 'PAUSED', meta.replace('profile_crc=11', 'profile_crc=-1')),
        fields(1, 'state', 'PAUSED', meta)+'\r\r',
        fields(1, 'state', 'PAUSED', meta)+'é',
        fields(1, 'access', 'END', 'rc=0 elapsed=0'),
        fields(1, 'inject', 'BEGIN', 'step=0 addr=0'),
        fields(1, 'diag', 'TX', diag.replace('sent=0', 'sent=2')),
        fields(1, 'diag', 'TX', diag.replace('rc=0', 'rc=-2147483649')),
        fields(1, 'diag', 'TX', diag.replace('fn=NONE', 'fn=other')),
        fields(1, 'state', 'RESTORING', meta),
        fields(1, 'state', 'RESTORING', meta+' cleanup_seconds=-1'),
    ]
    for text in invalid:
        r = m.Records(7)
        try:
            r.line(text)
        except m.RecoverError as exc:
            first = str(exc)
        else:
            raise AssertionError('accepted malformed '+text)
        assert r.data['seq'] == 0 and r.data['invalid'] == first
        try:
            r.line(fields(1, 'state', 'PAUSED', meta))
        except m.RecoverError as exc:
            assert str(exc) == first
        else:
            raise AssertionError('sticky error cleared')
        try:
            m.Records(7, r.data)
        except m.RecoverError as exc:
            assert str(exc) == first
        else:
            raise AssertionError('saved corruption accepted')
    virtual = m.Records(7, virtual=True)
    virtual.line(fields(1, 'inject', 'BEGIN', 'step=0 addr=0'))
    assert virtual.data['stack'] == []
    continued = m.Records(7, virtual.data)
    continued.line(fields(2, 'inject', 'BEGIN', 'step=0 addr=0'))
    assert continued.data['virtual'] is True
    # END identity equality and exact stack order, with singleton rows not popping.
    for begin, end in [
        (nested[0], ('gf_pre', 'END', 'main=0 pc=2 opcode=2 exec=1 substep=3 rc=0')),
        (nested[1], ('gf_bus', 'END', 'pc=1 bar=1 off=2 width=4 readback=1 rc=0')),
        (nested[2], ('screen', 'END', 'rc=0 elapsed=0')),
        (nested[2], ('state', 'PAUSED', meta)),
        (nested[2], ('final', 'END', final)),
    ]:
        r = m.Records(7); r.line(fields(1, *begin))
        r.line(fields(2, 'receive', 'DISCARD', 'discarded_bytes=0'))
        assert len(r.data['stack']) == 1
        try:
            r.line(fields(3, *end))
        except m.RecoverError:
            pass
        else:
            raise AssertionError('mismatched end accepted')
    # Resume stores the stack and leaves profile metadata frozen across RESTORING.
    r = m.Records(7);r.line(fields(1, 'state', 'PAUSED', meta))
    resumed = m.Records(7, r.data)
    resumed.line(fields(2, 'state', 'RESTORING', meta+' cleanup_seconds=17'))
    assert resumed.data['cleanup_seconds'] == 17
    resumed.line(fields(3, 'diag', 'TX', diag));resumed.line(fields(4, 'final', 'END', final))
    assert resumed.success()
    for text in (fields(5, 'final', 'END', final), fields(5, 'state', 'PAUSED', meta)):
        fresh = m.Records(7, resumed.data)
        try:
            fresh.line(text)
        except m.RecoverError:
            pass
        else:
            raise AssertionError('REC after final accepted')
    r = m.Records(7);r.line(fields(1, 'state', 'PAUSED', meta))
    fresh = m.Records(7, r.data)
    try:
        fresh.line(fields(2, 'state', 'RESTORING', meta.replace('profile_crc=11', 'profile_crc=12')+' cleanup_seconds=17'))
    except m.RecoverError:
        pass
    else:
        raise AssertionError('cleanup metadata changed')
    for previous in ({'sid':7, 'seq':1, 'crc':{}}, [], {'format':2}):
        try:
            m.Records(7, previous)
        except m.RecoverError:
            pass
        else:
            raise AssertionError('old state accepted')
    def exchange(data, good=False, split=256):
        p = Port(data, split=split);raw = io.BytesIO();saved=[]
        r = m.Records(7)
        with redirect_stderr(io.StringIO()) as warnings:
            try:
                result = m.exchange(p, 'recover now 7', r, 10, 1, raw, lambda d: saved.append(dict(d)), p.clock)
            except m.RecoverError as exc:
                assert not good, str(exc)
            else:
                assert good, result
        assert raw.getvalue() == data
        return saved, warnings.getvalue()
    body = row(1, 'state', 'RESTORING', meta+' cleanup_seconds=17')+row(2, 'diag', 'TX', diag)+row(3, 'final', 'END', final)
    for split in (1, 256):
        saved, warning = exchange((ECHO+body+'\x04').encode(), good=True, split=split)
        assert '--cleanup-timeout' in warning and '22' in warning
        assert saved[-1]['cleanup_seconds'] == 17
        exchange((ECHO+body.replace('\n', '\r\n')+'\x04').encode(), good=True, split=split)
        # Missing diagnostic, missing prefix/LF, partial token, empty row deletion.
        for damaged in (body.replace(row(2, 'diag', 'TX', diag), ''), body.replace('REC sid=7 seq=2', 'sid=7 seq=2'),
                        body.replace('event=TX ', 'event=T '), body.replace('\nREC sid=7 seq=2', 'REC sid=7 seq=2'),
                        body.replace('seq=3', 'seq=4'), body.replace('phase=diag event=TX', 'phase=diag event=TX')+'REC sid=7',
                        body.replace('fn=NONE', 'fn=PUT')):
            exchange((ECHO+damaged+'\x04').encode(), split=split)
    saved, _ = exchange((ECHO+'x'*(m.MAX_LINE+1)+'\n'+body+'\x04').encode())
    assert saved[-1]['invalid'] == 'overlong serial line'
    # Main catches damaged persisted JSON before opening the serial port.
    with tempfile.TemporaryDirectory(prefix='recover_state_') as tmp:
        state=Path(tmp)/'state';state.write_text('{')
        argv=['rshell_recover', '--session', '7', '--image-crc', '1234abcd', '--state', str(state), '--log', str(Path(tmp)/'raw')]
        with patch.object(sys, 'argv', argv), patch.object(m, 'open_port') as opened, redirect_stdout(io.StringIO()):
            assert m.main() == 1
            opened.assert_not_called()


def vpll_cases(m):
    for version in (4, m.VPLL_VERSION_1, m.VPLL_VERSION):
        for i, offset in enumerate(m.VPLL_OFFSETS):
            for value in (0xffffffff, 0x80000e02):
                rec = m.Records(7)
                rec.line(META.replace('version=1', 'version=%d' % version))
                rec.line('REC sid=7 seq=2 phase=gf_pre event=BEGIN main=2 pc=58915 opcode=75 exec=1 substep=%d' % (m.VPLL_FIRST+i))
                rec.line('REC sid=7 seq=3 phase=gf_bus event=BEGIN pc=58915 bar=0 off=%d width=4 old=1 new=2' % offset)
                try:
                    rec.line('REC sid=7 seq=4 phase=gf_bus event=END pc=58915 bar=0 off=%d width=4 readback=%d rc=0' % (offset, value))
                except m.RecoverError:
                    assert version not in (m.VPLL_VERSION_1, m.VPLL_VERSION) and value != 0xffffffff
                else:
                    assert version in (m.VPLL_VERSION_1, m.VPLL_VERSION) or value == 0xffffffff

    for old in (4, 5):
        rec = m.Records(7)
        rec.line(META.replace('version=1', 'version=%d' % m.VPLL_VERSION))
        rec.line('REC sid=7 seq=2 phase=gf_pre event=BEGIN main=2 pc=58915 opcode=75 exec=1 substep=%d' % m.VPLL_STAGE2_FIRST)
        rec.line('REC sid=7 seq=3 phase=gf_bus event=BEGIN pc=58915 bar=0 off=6817036 width=4 old=%d new=516' % old)
        rec.line('REC sid=7 seq=4 phase=gf_bus event=END pc=58915 bar=0 off=6817036 width=4 readback=516 rc=0')
        assert rec.data['vpll_comparison_valid'] == (old == 4)

def mode_c_cases(m):
    assert m.mode_c_timeout(9600, 23) == 1026
    assert m.mode_c_timeout(19200, 23) == 557
    def run(fault=None):
        rec = m.Records(7)
        rec.data['mode_c_active'] = True
        rec.data['mode_c_evidence'] = dict(vram=[], route=0, cell=False, cell_steps=[], checks=0, registers={})
        rows = []
        def add(phase, event, fields):
            rows.append('phase=%s event=%s %s' % (phase, event, fields))
        def pair(phase, fields, end):
            add(phase, 'BEGIN', fields)
            add(phase, 'END', end)
        for direction in (1, 0):
            f = 'pc=4294967295 bar=1 off=0 len=307200 dir=%d' % direction
            pair('gf_bus', f, f + ' bad_off=4294967295 expected=4294967295 actual=4294967295 rc=0')
        pair('gf_bus', 'pc=4294967295 bar=0 row=1 off=6296512 len=1 dir=1',
             'pc=4294967295 bar=0 row=1 off=6296512 len=1 dir=1 rc=0')
        pair('wait', 'step=1 addr=10', 'rc=0 elapsed=1')
        for step in (2, 7):
            pair('cell', 'step=%d addr=0' % step, 'rc=0 elapsed=1')
        pair('route', 'step=9 addr=4012', 'source=2 key=4294967295 mask=3 expected=1 actual=1 rc=0')
        for key, off, mask, value in ((10, 0x68050c, 0xefffffff, 0x10000204),
                                      (11, 0x680508, 0x7ffff, 123),
                                      (12, 0x680578, 0x8000ffff, 456)):
            pair('access', 'step=%d addr=%d' % (key, off), 'rc=0 elapsed=1')
            add('check', 'END', 'source=0 key=%d mask=%d expected=%d actual=%d rc=0' % (key, mask, value, value))
        for i in range(61):
            add('check', 'END', 'source=1 key=4294967295 mask=4294967295 expected=1 actual=1 rc=0')
        add('check', 'END', 'source=2 key=4294967295 mask=3 expected=1 actual=1 rc=0')
        add('check', 'END', 'source=3 key=4294967295 mask=4294967295 expected=1 actual=1 rc=0')
        if fault == 'check':
            i = next(i for i, x in enumerate(rows) if 'source=1 ' in x)
            rows[i] = rows[i].replace('actual=1', 'actual=0')
        if fault == 'route': rows = [x.replace('mask=3 expected=1 actual=1', 'mask=3 expected=1 actual=0') for x in rows]
        if fault == 'cell': rows[-1] = rows[-1].replace('actual=1', 'actual=0')
        if fault == 'missing': rows.pop(1)
        try:
            for seq, row in enumerate(rows, 1):
                rec.line('REC sid=7 seq=%d %s' % (seq, row))
            assert rec.mode_c_success()
        except (m.RecoverError, AssertionError):
            assert fault is not None
        else:
            assert fault is None
            lines = ['REC sid=7 seq=%d %s\n' % (i, row) for i, row in enumerate(rows, 1)]
            lines.append('REC sid=7 seq=%d phase=state event=PAUSED version=1 profile_crc=11 plan_hash=12 snapshot_crc=13\n' % (len(rows) + 1))
            port = Port(('> gfinit modeset 7 --allow-write\n' + ''.join(lines) + '\x04').encode())
            fresh = m.Records(7)
            fresh.data['mode_c_active'] = True
            fresh.data['mode_c_trial'] = True
            fresh.data['mode_c_evidence'] = dict(vram=[], route=0, cell=False, cell_steps=[], checks=0, registers={})
            with redirect_stdout(io.StringIO()) as output:
                assert m.exchange(port, 'gfinit modeset 7 --allow-write', fresh, 100, 23, io.BytesIO(), lambda _: None, port.clock) == 'PAUSED'
            assert '機械項目合格・映像保留' in output.getvalue()
            assert not fresh.data['mode_c_active'] and fresh.data['mode_c_trial']
            cleanup_cases(m, fresh.data)

    for fault in (None, 'check', 'route', 'cell', 'missing'):
        run(fault)


def cleanup_cases(m, saved):
    class DelayedPort(Port):
        def __init__(self, command, seq):
            super().__init__(('> '+command+'\n').encode())
            self.seq, self.stage = seq, 0
            self.last = 0
        def read(self, count):
            if not self.data and self.stage == 0:
                self.data.extend(('REC sid=7 seq=%d phase=state event=RESTORING version=1 profile_crc=11 plan_hash=12 snapshot_crc=13 cleanup_seconds=3\n' % self.seq).encode())
                self.stage = 1
                self.last = self.t
            elif not self.data and self.stage == 1 and self.t > self.last + 2:
                self.data.extend((DIAG.replace('seq=2', 'seq=%d' % (self.seq+1)) + FINAL.split('\n', 1)[1].replace('seq=3', 'seq=%d' % (self.seq+2)) + '\x04').encode())
                self.stage = 2
            return super().read(count)
    for is_c in (True, False):
        for abort in (True, False):
            state = dict(saved)
            state['mode_c_trial'] = is_c
            records = m.Records(7, state)
            command = 'gfinit abort 7 --allow-write' if abort else 'recover now 7'
            port = DelayedPort(command, state['seq'] + 1)
            raw = io.BytesIO()
            try:
                with redirect_stderr(io.StringIO()):
                    result = m.exchange(port, command, records, 1, 1, raw, lambda _: None, port.clock)
            except m.RecoverError:
                assert not is_c
            else:
                assert is_c and result == 'OK'
            assert (b'phase=final' in raw.getvalue()) == is_c, (is_c, abort, port.t, raw.getvalue(), records.data['invalid'])
            assert (m.abort_frame(7) in port.writes) == (not is_c)


def mode_c_cli_cases(m):
    with tempfile.TemporaryDirectory(prefix='recover_cli_') as tmp:
        state = Path(tmp)/'state'
        log = Path(tmp)/'log'
        for whitespace in ('  ', '\t'):
            command = 'gfinit' + whitespace + 'modeset 7 --allow-write'
            previous = m.Records(7).data
            previous['image_crc'] = '1234abcd'
            state.write_text(json.dumps(previous))
            port = Port(('> '+command+'\n'+META+'\x04').encode())
            port.close = lambda: None
            argv = ['rshell_recover', '--session', '7', '--image-crc', '1234abcd', '--state', str(state), '--log', str(log), '--command', command, '--timeout', '1', '--cleanup-timeout', '23']
            with patch.object(sys, 'argv', argv), patch.object(m, 'open_port', return_value=port), patch.object(m, 'exchange', wraps=m.exchange) as exchange, redirect_stdout(io.StringIO()):
                assert m.main() == 1  # PAUSED without C evidence is rejected.
            assert exchange.call_args.args[3] == m.mode_c_timeout(9600, 23)
            assert json.loads(state.read_text())['mode_c_trial']


MUTATIONS = [
    ('mode_c_split', "' '.join(command.split())", 'command'),
    ('mode_c_cleanup', "if records.data.get('mode_c_trial'):", "if records.data.get('mode_c_active'):"),
    ('mode_c_check', '(actual ^ expected) & mask', '0'),
    ('vpll_version', 'version not in (VPLL_VERSION_1, VPLL_VERSION)', 'False'),
    ('session', "sid != candidate_sid or seq != self.data['seq'] + 1", "seq != self.data['seq'] + 1"),
    ('sequence', "sid != candidate_sid or seq != self.data['seq'] + 1", "sid != candidate_sid"),
    ('required', "if name not in fields:  # Mandatory fields must precede state updates.", "if False:  # Mandatory fields must precede state updates."),
    ('pop', '                stack.pop()', '                pass'),
    ('identity', "or any(stack[-1][key] != fields[key] for key in ids)", 'or False'),
    ('fragment', "if line or dropping:\n                    record_failure('truncated line before EOT/deadline')", "if False:\n                    record_failure('truncated line before EOT/deadline')"),
    ('loss', "f.get('lost_lines') == '0'", 'True'),
    ('verify', "f.get('verified') == '1'", 'True'),
    ('echo', 'if not echo:', 'if False:'),
    ('status-body', "raise RecoverError('missing/malformed status reply')", "return 'accepted'"),
    ('serial-owner', 'exclusive=True', 'exclusive=False'),
    ('timeout', "record_failure('host timeout')", 'pass'),
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
            protocol_cases(mutant)
            vpll_cases(mutant)
            mode_c_cases(mutant)
            mode_c_cli_cases(mutant)
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
    protocol_cases(load(SRC))
    vpll_cases(load(SRC))
    mode_c_cases(load(SRC))
    mode_c_cli_cases(load(SRC))
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
