#!/usr/bin/env python3
"""T4b terminal-loss experiment: continue an existing normal session with end.

One port reader records unfiltered --raw, then supplies --drop none/final/eot
bytes to the unchanged rshell_recover monitor (--log). Prints abort_frames.
Use the state/log from begin and step.
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
        self.abort_frames = 0

    def write(self, data):
        self.abort_frames += data == self.abort
        return self.port.write(data)

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
    parser.add_argument('--drop', choices=('none', 'final', 'eot'), required=True)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--cleanup-timeout', type=float, default=40)
    args = parser.parse_args(argv)
    def save(data):
        args.state.write_text(json.dumps(data, sort_keys=True) + '\n')

    try:
        previous = json.loads(args.state.read_text())
        records = recover.Records(args.session, previous)
        if previous.get('image_crc') != args.image_crc.lower():
            raise recover.RecoverError('saved boot CRC differs')
        with closing(recover.open_port(args.port, args.baud)) as port, \
                args.raw.open('ab') as raw, args.log.open('ab') as log:
            filtered = FilterPort(port, raw, args.drop, args.session)
            try:
                result = recover.exchange(filtered, 'recover virtual end %d' % args.session,
                                          records, args.timeout, args.cleanup_timeout,
                                          log, save, clock)
            finally:
                print('abort_frames=%d' % filtered.abort_frames)
        if result != 'OK':
            raise recover.RecoverError('end did not reach verified terminal')
        print(result)
        return 0
    except (recover.RecoverError, OSError, ValueError) as exc:
        print('recover: ' + str(exc))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
