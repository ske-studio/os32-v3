#!/usr/bin/env python3
"""T4b terminal-loss experiment: continue an existing normal session with end.

One port reader records unfiltered --raw, then supplies --drop none/final/eot
bytes to the unchanged rshell_recover monitor (--log). --stats records abort
write calls and frame attempts. Use the state/log from begin and step.
"""
import argparse
from contextlib import closing
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import rshell_recover as recover


class FilterPort:
    def __init__(self, port, raw, drop, session):
        self.port, self.raw, self.drop = port, raw, drop
        self.pending = bytearray()
        self.abort = recover.abort_frame(session)
        self.remaining = 0
        self.stats = dict(abort_frames=0, abort_writes=0)

    def write(self, data):
        if data == self.abort:
            self.stats['abort_frames'] += 1
            self.remaining = len(data)
        if self.remaining:
            self.stats['abort_writes'] += 1
        count = self.port.write(data)
        if count and self.remaining:
            self.remaining -= count
        return count

    def read(self, count):
        chunk = self.port.read(count)
        self.raw.write(chunk)
        self.raw.flush()
        if self.drop == 'none':
            return chunk
        if self.drop == 'eot':
            return chunk.replace(b'\x04', b'')
        out = bytearray()
        for byte in chunk:
            if byte in (10, 4):
                line = bytes(self.pending)
                self.pending.clear()
                fields = line.rstrip(b'\r').split()
                final = (line.startswith(b'REC ') and b'phase=final' in fields
                         and b'event=END' in fields)
                if not final:
                    out.extend(line)
                if byte == 4 or not final:
                    out.append(byte)
            else:
                self.pending.append(byte)
                if len(self.pending) > 4096:
                    raise recover.RecoverError('overlong serial line')
        return bytes(out)


def main(argv=None, clock=time.monotonic):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--baud', type=int, default=9600)
    parser.add_argument('--session', type=int, required=True)
    parser.add_argument('--image-crc', required=True)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--stats', type=Path, required=True)
    parser.add_argument('--drop', choices=('none', 'final', 'eot'), required=True)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--cleanup-timeout', type=float, default=40)
    args = parser.parse_args(argv)
    if not 0 < args.session <= 0xffffffff or min(args.timeout, args.cleanup_timeout) <= 0:
        parser.error('positive session and deadlines required')
    paths = (args.state, args.log, args.raw, args.stats)
    if len({p.resolve() for p in paths}) != len(paths):
        parser.error('state/log/raw/stats must be separate files')
    previous = json.loads(args.state.read_text())
    records = recover.Records(args.session, previous)
    if previous.get('image_crc') != args.image_crc.lower():
        raise recover.RecoverError('saved boot CRC differs')

    def save(data):
        args.state.write_text(json.dumps(data, sort_keys=True) + '\n')

    with closing(recover.open_port(args.port, args.baud)) as port, \
            args.raw.open('ab') as raw, args.log.open('ab') as log:
        filtered = FilterPort(port, raw, args.drop, args.session)
        try:
            result = recover.exchange(filtered, 'recover virtual end %d' % args.session,
                                      records, args.timeout, args.cleanup_timeout,
                                      log, save, clock)
            if result != 'OK':
                raise recover.RecoverError('end did not reach verified terminal')
            rc = 0
        except (recover.RecoverError, OSError) as exc:
            print('recover: ' + str(exc))
            rc = 1
        finally:
            args.stats.write_text(json.dumps(filtered.stats, sort_keys=True) + '\n')
        return rc


if __name__ == '__main__':
    raise SystemExit(main())
