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
                     ('txd=0','txd=1'),
                     ('fn=NONE','fn=PUT')]:
        run(ECHO+META+FINAL.replace(old,new)+'\x04')
    run(ECHO+META+FINAL.replace('result=OK','result=PARTIAL').replace('unknown=0','unknown=1').replace('override=0','override=1').replace('trial=0','trial=1')+'\x04', good=True)
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
                (meta+final.replace('result=OK', 'result=PARTIAL'), True),
                (meta+final.replace('override=0', 'override=1'), True)]:
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
    # A failed file-based A2 retry must not reuse a saved PAUSED as success.
    # The rshell response contains the CLI diagnostic and EOT, with no new REC.
    command = 'gfinit prepare 7 --mode-c --allow-write --no-verify-file /missing.txt'
    errors = [
        'gfinit: rejected rc=-1 invalid no-verify file (MMIO/CR/SR hex, max 64 entries)',
        'gfinit: rejected rc=-10 kdebug unavailable',
    ]
    with tempfile.TemporaryDirectory(prefix='gfinit_retry_') as tmp:
        state = Path(tmp) / 'state.json'
        log = Path(tmp) / 'raw.log'
        for split in (1, 7, 256):
            for error in errors:
                records = m.Records(7)
                records.line(META)
                records.data['image_crc'] = '1234abcd'
                state.write_text(json.dumps(records.data))
                data = ('> ' + command + '\n' + error + '\n\x04').encode()
                port = Port(data, split=split)
                closed = []
                port.close = lambda: closed.append(True)
                argv = ['rshell_recover', '--session', '7', '--image-crc', '1234abcd',
                        '--state', str(state), '--log', str(log), '--command', command]
                with patch.object(sys, 'argv', argv), patch.object(m, 'open_port', return_value=port), redirect_stdout(io.StringIO()) as out:
                    assert m.main() == 1
                assert out.getvalue().splitlines() == [
                    'REC RUN max interval: n/a s after phase=n/a',
                    'recover: guest rejected recovery']
                assert json.loads(state.read_text())['invalid'] == 'guest rejected recovery'
                assert port.writes == [(command + '\n').encode()] and closed == [True]
                assert log.read_bytes().endswith(data)
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
        assert not warning
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


def mode_c_cases(m):
    # The monitor copies arbitrary guest observations, without a register oracle.
    for mode in ('record', 'required'):
        for result in ('OK', 'NG', None):
            command = 'gfinit modeset 7 --allow-write'
            rows = ('REC sid=7 seq=1 phase=check event=END source=0 key=9 mask=255 expected=1 actual=99 rc=1\n'
                    'REC sid=7 seq=2 phase=state event=PAUSED version=1 profile_crc=11 plan_hash=12 snapshot_crc=13\n')
            guest = '' if result is None else 'GF C verify=%s result=%s\n' % (mode, result)
            port = Port(('> '+command+'\n'+rows+guest+'\x04').encode())
            rec = m.Records(7)
            with redirect_stdout(io.StringIO()) as output:
                try:
                    value = m.exchange(port,command,rec,10,1,io.BytesIO(),lambda _:None,port.clock)
                except m.RecoverError:
                    assert result != 'OK'
                else:
                    assert result == 'OK' and value == 'PAUSED'
                    assert guest.strip() in output.getvalue()
                    assert ('観測完了' if mode == 'record' else '機械項目合格・映像保留') in output.getvalue()
    # C's result remains mandatory after abort/S0, independently of S0's result.
    for verb in ('modeset 7', 'c-only'):
        command = 'gfinit '+verb+' --allow-write --verify record'
        for terminal in ('OK', 'PARTIAL'):
            rows = META.replace('event=PAUSED', 'event=RESTORING').rstrip('\n')+' cleanup_seconds=17\n'
            rows += FINAL.replace('result=OK', 'result='+terminal).replace('trial=0', 'trial=1')
            for result in (None, 'NG', 'OK'):
                guest = '' if result is None else 'GF C verify=record result='+result+'\n'
                port = Port(('> '+command+'\n'+rows+guest+'\x04').encode())
                with redirect_stdout(io.StringIO()):
                    try:
                        value = m.exchange(port,command,m.Records(0),10,1,io.BytesIO(),lambda _:None,port.clock)
                    except m.RecoverError as exc:
                        assert result != 'OK', str(exc)
                        assert 'guest C' in str(exc)
                    else:
                        assert result == 'OK' and value == terminal
    assert not hasattr(m,'MODE_C_CHECK_COUNT') and not hasattr(m.Records,'mode_c_success')


def mode_c_cli_cases(m):
    with tempfile.TemporaryDirectory(prefix='recover_cli_') as tmp:
        state, log = Path(tmp)/'state', Path(tmp)/'log'
        for command, expected in [('gfinit  modeset 7 --allow-write',1800),
                                  ('gfinit\trun-one 7 4 --allow-write',900),
                                  ('gfinit size-bars 7 --allow-write',240),
                                  ('gfinit abort 7 --allow-write',180)]:
            previous = m.Records(7).data
            previous['image_crc']='1234abcd'
            state.write_text(json.dumps(previous))
            port=Port(b'');port.close=lambda:None
            argv=['monitor','--session','7','--image-crc','1234abcd','--state',str(state),'--log',str(log),'--command',command]
            with patch.object(sys,'argv',argv),patch.object(m,'open_port',return_value=port),patch.object(m,'exchange',return_value='PAUSED') as exchange,redirect_stdout(io.StringIO()):
                assert m.main()==0
            assert exchange.call_args.args[3]==expected


def restore_cases(m):
    assert m.RESTORE_IDLE_SECONDS == 30
    # rshell emits no new EOT while idle: reply only after a command is written.
    class Shell(Port):
        def write(self, data):
            count = super().write(data)
            if data == b'recover restore\n':
                self.restore_at = self.t
                self.data += b'> recover restore\n' + (META + FINAL).encode() + b'\x04'
            return count

    command = 'gfinit c-only --allow-write --verify required'
    for terminal in ('PAUSED', 'S0'):
        rows = META + (FINAL if terminal == 'S0' else '')
        reply = '> ' + command + '\n' + rows + 'GF C verify=required result=OK\n\x04'
        port = Shell(reply.encode())
        with redirect_stdout(io.StringIO()):
            assert m.exchange(port, command, m.Records(0), 20, 1,
                              io.BytesIO(), lambda _: None, port.clock) == (
                                  'PAUSED' if terminal == 'PAUSED' else 'OK')
        assert not port.data
        port.writes.clear()
        idle_at = port.t
        raw = io.BytesIO()
        assert m.restore_exchange(port, 60, 1, raw, port.clock) == 'OK'
        assert port.writes == [b'recover restore\n']
        assert 30 <= port.restore_at - idle_at < 30.1
        assert raw.getvalue() == b'> recover restore\n' + (META + FINAL).encode() + b'\x04'

    with tempfile.TemporaryDirectory(prefix='restore_cli_') as tmp:
        state, log=Path(tmp)/'state',Path(tmp)/'log'
        for bad in ('{broken',json.dumps(dict(invalid='old failure',image_crc='wrong')), '[]'):
            for broken_log in (False,True):
                state.write_text(bad)
                command='recover restore'
                rows=META.replace('seq=1','seq=41')+DIAG.replace('seq=2','seq=42')+FINAL.split('\n',1)[1].replace('seq=3','seq=43').replace('result=OK','result=PARTIAL')
                port=Port(('\x04> '+command+'\n'+rows+'\x04').encode());port.close=lambda:None
                argv=['monitor','--restore','--state',str(state),'--image-crc','invalid','--log',str(log/'missing' if broken_log else log)]
                with patch.object(sys,'argv',argv),patch.object(m,'open_port',return_value=port),redirect_stdout(io.StringIO()):
                    assert m.main()==0
                assert port.writes==[b'recover restore\n']
                assert state.read_text()==bad
        # Without EOT it still sends restore and the existing sid abort on timeout.
        port=Port(b'\x04> recover restore\n'+META.encode());port.close=lambda:None
        argv=['monitor','--restore','--log',str(log),'--timeout','.1','--cleanup-timeout','.1']
        restore=m.restore_exchange
        with patch.object(sys,'argv',argv),patch.object(m,'open_port',return_value=port),patch.object(m,'restore_exchange',wraps=lambda *a: restore(*a,clock=port.clock)),redirect_stdout(io.StringIO()):
            assert m.main()==1
        assert port.writes[0]==b'recover restore\n'

    # A current RUN stream uses the existing abort frame, with no shell command.
    running=META.replace('event=PAUSED','event=RUNNING')
    # Late bytes restart the quiet interval, including an unfinished row.
    class LateShell(Shell):
        def __init__(self, pending):
            super().__init__(b'')
            self.pending = list(pending)
            self.last_rx = 0
        def read(self, count):
            self.t += .25
            if self.pending and self.t >= self.pending[0][0]:
                self.data += self.pending.pop(0)[1]
            chunk = super().read(count)
            if chunk:
                self.last_rx = self.t
            return chunk
        def write(self, data):
            self.quiet_since = self.last_rx
            return super().write(data)

    partial = b'7 seq=20 phase=access'
    tail = b' event=END rc=0 elapsed=1\n' + (running + FINAL + '\x04').encode()
    port = LateShell([(8, partial), (17, tail)])
    raw = io.BytesIO()
    assert m.restore_exchange(port, 30, 1, raw, port.clock) == 'OK'
    assert port.writes == [m.abort_frame(7)]
    assert raw.getvalue() == partial + tail
    for prefix in (b'noise\n', partial, b'x' * (m.MAX_LINE + 1)):
        port = LateShell([(8, prefix)])
        raw = io.BytesIO()
        assert m.restore_exchange(port, 60, 1, raw, port.clock) == 'OK'
        assert port.writes == [b'recover restore\n']
        assert 30 <= port.restore_at - port.quiet_since < 30.3
        assert raw.getvalue() == prefix + b'> recover restore\n' + (META + FINAL).encode() + b'\x04'

    # r3's real-core fixture: attach after VRAM BEGIN, then 14.4 s without REC.
    # The unseen BEGIN still makes parsing fail, but abort must use the live sid.
    end = ('REC sid=1 seq=2 phase=gf_bus event=END pc=4294967295 bar=1 off=0 '
           'len=307200 dir=1 bad_off=4294967295 expected=4294967295 '
           'actual=4294967295 rc=0\n')
    paused = META.replace('sid=7 seq=1', 'sid=1 seq=3')
    class QuietRun(Port):
        def __init__(self):
            super().__init__(b'')
            self.released = False
            self.write_times = []
        def read(self, count):
            if self.t >= 14.4 and not self.released:
                self.data += (end + paused + '\x04').encode()
                self.released = True
            return super().read(count)
        def write(self, data):
            self.write_times.append(self.t)
            return super().write(data)  # RUN discards shell text; no echo/reply.

    port = QuietRun()
    with redirect_stdout(io.StringIO()):
        try:
            m.restore_exchange(port, 20, 1, io.BytesIO(), port.clock)
        except m.RecoverError as exc:
            assert 'unmatched BEGIN/END' in str(exc)
        else:
            raise AssertionError('unseen VRAM BEGIN accepted')
    assert port.writes == [m.abort_frame(1)]
    assert 14.4 <= port.write_times[0] < 14.5

    port=Port((running+FINAL+'\x04').encode(),split=len(running))
    with redirect_stdout(io.StringIO()):
        assert m.restore_exchange(port,10,1,io.BytesIO(),port.clock)=='OK'
    assert port.writes==[m.abort_frame(7)]
    # Empty reads and attachment mid-row do not establish shell readiness.
    fragment = b'7 seq=20 phase=access event=END rc=0 elapsed=1\n'
    for prefix in (b'', fragment, b'x sid=8 seq=1 phase=state event=RUNNING version=1 profile_crc=11 plan_hash=12 snapshot_crc=13\n'):
        for split in (1, 7, 256):
            data = prefix+(running+FINAL+'\x04').encode()
            port=Port(data,split=split,idle_reads=2);raw=io.BytesIO()
            assert m.restore_exchange(port,10,1,raw,port.clock)=='OK'
            assert port.writes==[m.abort_frame(7)]
            assert raw.getvalue()==data
    for data in (b'', b'REC sid=7 seq=', fragment, META.encode(), FINAL.encode()):
        port=Port(data,split=7);raw=io.BytesIO()
        try:
            m.restore_exchange(port,.5,.5,raw,port.clock)
        except m.RecoverError as exc:
            assert 'physical recovery' in str(exc) or 'missing' in str(exc)
        else:
            raise AssertionError('unknown state accepted')
        # A diagnostic before final is an active stream and may request abort.
        assert port.writes==([m.abort_frame(7)] if data==FINAL.encode() else [])
        assert raw.getvalue()==data


def interval_cases(m):
    class TimedPort(Port):
        def __init__(self, pending):
            super().__init__(b'')
            self.pending = list(pending)
        def clock(self):
            return self.t
        def read(self, count):
            if not self.data:
                if not self.pending:
                    self.t += 1
                    return b''
                self.t, data = self.pending.pop(0)
                self.data += data
            chunk = bytes(self.data[:count])
            del self.data[:len(chunk)]
            return chunk

    # Ignore time before RUN and non-REC progress; report even on missing EOT.
    for gap in (14.4, 35):
        for eot in ('\x04', ''):
            pending = [(0, ECHO + META),
                       (100, META.replace('seq=1', 'seq=2').replace('PAUSED', 'RUNNING')),
                       (102, 'REC sid=7 seq=3 phase=gf_bus event=BEGIN pc=1 bar=1 off=0 len=307200 dir=1\n'),
                       (109, 'VRAM transfer in progress\n'),
                       (102 + gap, 'REC sid=7 seq=4 phase=gf_bus event=END pc=1 bar=1 off=0 len=307200 dir=1 bad_off=0 expected=0 actual=0 rc=0\n'),
                       (103 + gap, META.replace('seq=1', 'seq=5') + eot)]
            port = TimedPort([(at, row.encode()) for at, row in pending])
            with redirect_stdout(io.StringIO()) as output:
                try:
                    result = m.exchange(port, 'recover now 7', m.Records(7), 180, 1,
                                        io.BytesIO(), lambda _: None, port.clock)
                except m.RecoverError as exc:
                    assert not eot and 'host timeout' in str(exc)
                else:
                    assert eot and result == 'PAUSED'
            assert output.getvalue().splitlines() == [
                'REC RUN max interval: %.3f s after phase=gf_bus' % gap]
            assert port.writes == [b'recover now 7\n'] + ([m.abort_frame(7)] if not eot else [])

    # Two rows received in the same chunk have a measured zero interval.
    rows = META.replace('PAUSED', 'RUNNING') + META.replace('seq=1', 'seq=2')
    for rows, expected in ((rows, '0.000 s after phase=state'),
                           (META, 'n/a s after phase=n/a')):
        port = TimedPort([(0, (ECHO + rows + '\x04').encode())])
        with redirect_stdout(io.StringIO()) as output:
            assert m.exchange(port, 'recover now 7', m.Records(7), 10, 1,
                              io.BytesIO(), lambda _: None, port.clock) == 'PAUSED'
        assert output.getvalue().strip() == 'REC RUN max interval: ' + expected


MUTATIONS = [
    ('guest_c', "if c_result is None or c_result[2] != 'OK':", 'if False:'),
    ('session', "sid != candidate_sid or seq != self.data['seq'] + 1", "seq != self.data['seq'] + 1"),
    ('sequence', "sid != candidate_sid or seq != self.data['seq'] + 1", "sid != candidate_sid"),
    ('required', "if name not in fields:  # Mandatory fields must precede state updates.", "if False:  # Mandatory fields must precede state updates."),
    ('pop', '                stack.pop()', '                pass'),
    ('identity', "or any(stack[-1][key] != fields[key] for key in ids)", 'or False'),
    ('fragment', "if line or dropping:\n                        record_failure('truncated line before EOT/deadline')", "if False:\n                        record_failure('truncated line before EOT/deadline')"),
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
            mode_c_cases(mutant)
            mode_c_cli_cases(mutant)
            restore_cases(mutant)
            interval_cases(mutant)
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
    mode_c_cases(load(SRC))
    mode_c_cli_cases(load(SRC))
    restore_cases(load(SRC))
    interval_cases(load(SRC))
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
