#!/usr/bin/env python3
"""Fake serial/time tests of the actual recover host; --mutate kills guards."""
import argparse
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import zlib

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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cases(load(SRC))
    print('recover host: PASS')
    if args.mutate:
        mutations = [
            ('session', "sid != self.data['sid'] or seq != self.data['seq'] + 1", "seq != self.data['seq'] + 1"),
            ('sequence', "sid != self.data['sid'] or seq != self.data['seq'] + 1", "sid != self.data['sid']"),
            ('loss', "f.get('lost_lines') == '0'", 'True'),
            ('verify', "f.get('verified') == '1'", 'True'),
            ('echo', 'if not echo:', 'if False:'),
            ('timeout', "failure = failure or 'host timeout'", 'failure = failure'),
        ]
        with tempfile.TemporaryDirectory(prefix='recover_host_') as tmp:
            for name, before, after in mutations:
                source = SRC.read_text()
                assert source.count(before) == 1
                path = Path(tmp) / (name + '.py')
                path.write_text(source.replace(before, after))
                mutant = load(path)
                try:
                    cases(mutant)
                except (AssertionError, mutant.RecoverError):
                    print('killed:', name)
                else:
                    raise AssertionError('survived: ' + name)


if __name__ == '__main__':
    main()
