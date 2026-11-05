#!/usr/bin/env python3
"""Host notification wire, timing, queue, and mutation tests."""
CHECK = dict(name='host-notify', tier=1, needs=[],
             covers=['tools/host_notify.py', 'net/link.h'], budget=1)
import argparse
import importlib.util
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def load(path):
    spec = importlib.util.spec_from_file_location('host_notify', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Registry:
    def resolve(self, name):
        return {'os32.alive': 23, 'example': 42}[name]


def test(module):
    m = load(module)
    header = (ROOT / 'net/link.h').read_text()
    def value(name):
        return int(re.search(r'^#define\s+' + name + r'\s+(\d+)', header, re.M).group(1))
    assert m.OP_NOTIFY == value('LINK_OP_NOTIFY')
    assert m.DATA_MAX == value('LINK_NOTIFY_DATA_MAX')
    assert len({m.new_generation() for _ in range(8)}) == 8
    n = m.Notify(Registry(), 0x12345678)
    assert n.hello_extension() == bytes.fromhex('785634121700')
    n.session(9, 7, 0.0)
    assert n.tick(0) == []
    n.ack(9, 7, 0, 4)
    first = n.tick(0)
    assert len(first) == 1
    op, flags, seq, ack, rid, window, payload = first[0]
    wire = struct.pack('<BBHIIHIH', op, flags, 7, seq, ack, len(payload), rid, 9) + payload
    assert wire == bytes.fromhex('0b0007000100000000000000100000000000090078563412170001000400000078563412')
    for name, offset in [('OP', 0), ('FLAGS', 1), ('EPOCH', 2), ('SEQ', 4), ('ACK', 8), ('LENGTH', 12), ('RID', 14), ('SESS', 18)]:
        assert value('LINK_OFF_' + name) == offset
    for name, offset in [('HOST_GEN', 0), ('SERVICE', 4), ('KIND', 6), ('LENGTH', 8), ('RESERVED', 10), ('DATA', 12)]:
        assert value('LINK_NOTIFY_' + name + '_OFF') == offset
    assert n.tick(.099) == []
    assert n.tick(.1) == first
    n.ack(8, 7, 1, 4)
    assert n.tick(.2) == first
    n.ack(9, 7, 1, 4)
    assert n.tick(4.999) == []
    alive = n.tick(5.0)
    assert len(alive) == 1 and alive[0][2] == 2 and alive[0][-1] == payload
    n.ack(9, 7, 2, 4)
    assert len(n.tick(10.0)) == 1
    n.session(10, 8, 11.0)
    n.ack(10, 8, 0, 4)
    assert n.tick(11.0)[0][2] == 1
    for i in range(70):
        n.notify('example', 2, bytes([i]))
    assert len(n.pending) + len(n.inflight) == 64 and n.dropped == 7
    assert n.pending[0][-1] == 7
    frames = n.tick(11.0)
    assert [f[2] for f in frames] == [2, 3, 4]
    assert [f[2] for f in n.tick(11.1)] == [1, 2, 3, 4]
    n.ack(10, 8, 3, 4)
    assert [f[2] for f in n.tick(11.11)] == [5, 6, 7]
    print('PASS host notification')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/host_notify.py'))
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [('generation', 'return secrets.randbelow(0xffffffff) + 1', 'return 1'),
                     ('alive', "self.notify('os32.alive', 1, struct.pack('<I', self.generation))", 'pass')]
        with tempfile.TemporaryDirectory() as directory:
            for name, old, new in mutations:
                assert source.count(old) == 1
                path = Path(directory) / 'host_notify.py'
                path.write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, '-B', __file__, '--module', str(path)], capture_output=True, text=True)
                if result.returncode == 0 or 'AssertionError' not in result.stderr:
                    raise RuntimeError(f'{name}: mutation did not fail at runtime: {result.stderr}')
                print('RED', name)
    return 0


def run(ctx):
    argv = [sys.executable, '-B', __file__]
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
