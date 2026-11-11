#!/usr/bin/env python3
"""Exercise terminal host protocol and its mutation anchors."""
import importlib.util
import io
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/ppc_lan_host.py'
CHECK = dict(name='ppc-lan-terminal-host', tier=1, budget=60, needs=[],
             covers=['tools/ppc_lan_host.py'], inputs=['tools/ppc/test_lan_terminal_host.py'])
MUTATIONS = [
    ('split', 'range(0, len(data), PAYLOAD_MAX)', 'range(0, len(data), PAYLOAD_MAX + 1)', 'split'),
    ('gap', "self.diagnostic(f'output gap: {seq - max(self.out_next, self.state_next)}')", 'pass', 'gap'),
]


def exercise(path, directory):
    spec = importlib.util.spec_from_file_location('host', path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    class Fake:
        local_mac = b'local!'
        def __init__(self):
            self.sent = []
        def send(self, frame):
            self.sent.append(frame)
    fake = Fake()
    peer, boot = b'peer!!', b'b' * 16
    host = m.Host(fake, peer, directory, output=lambda _: None)
    host.receive(fake.local_mac + peer + struct.pack('!H', m.ETHERTYPE) + m.encode(m.BEACON, boot))
    display, diagnostics = io.BytesIO(), []
    term = m.Terminal(host, display=display, diagnostic=diagnostics.append)
    term.open()
    def frame(kind, nonce=None, **kw):
        return fake.local_mac + peer + struct.pack('!H', m.ETHERTYPE) + m.encode(kind, boot, nonce=host.nonce if nonce is None else nonce, **kw)
    assert m.decode(fake.sent[0][14:])['kind'] == m.TERM_OPEN
    term.send_bytes(b'ignored')
    assert len(fake.sent) == 1
    term.receive(frame(m.TERM_STATE, nonce=b'x' * 16, payload=bytes(24)))
    assert not term.connected
    term.receive(frame(m.TERM_STATE, payload=struct.pack('!QQQ', 0, 2, 0), drop_records=3, drop_bytes=4))
    assert term.connected and 'in_drop_frames=2' in diagnostics[-1] and 'output_drop_bytes=4' in diagnostics[-1]
    term.send_bytes(b'a' * 255 + b'\r' + b'b' * 257)
    messages = [m.decode(f[14:]) for f in fake.sent[1:]]
    if [len(x['payload']) for x in messages] != [256, 256, 1] or b''.join(x['payload'] for x in messages) != b'a' * 255 + b'\n' + b'b' * 257 or [x['seq'] for x in messages] != [0, 1, 2]:
        print('split'); return 1
    term.receive(frame(m.TERM_OUT, seq=0, payload=b'hello'))
    term.receive(frame(m.TERM_OUT, seq=2, payload=b' world'))
    if 'output gap: 1' not in diagnostics:
        print('gap'); return 1
    term.receive(frame(m.TERM_OUT, nonce=b'x' * 16, seq=3, payload=b'bad'))
    term.receive(frame(m.TERM_STATE, seq=5, payload=bytes(24)))
    assert diagnostics.count('output gap: 2') == 1
    assert display.getvalue() == b'hello world'
    assert (pathlib.Path(directory) / (boot.hex() + '.terminal')).read_bytes() == b'hello world'
    print('PASS')
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--exercise':
        return exercise(pathlib.Path(sys.argv[2]), sys.argv[3])
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory() as temp:
        def run(path):
            with tempfile.TemporaryDirectory(dir=temp) as out:
                return subprocess.run([sys.executable, '-B', __file__, '--exercise', str(path), out], capture_output=True, text=True)
        result = run(SOURCE)
        if result.returncode or result.stdout != 'PASS\n':
            raise RuntimeError(f'baseline: {result.returncode} {result.stdout} {result.stderr}')
        print('baseline PASS')
        if '--mutate' in sys.argv:
            for name, old, new, expected in MUTATIONS:
                if source.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor count {source.count(old)}')
                mutant = pathlib.Path(temp) / 'mutant.py'
                mutant.write_text(source.replace(old, new))
                result = run(mutant)
                if result.returncode != 1 or result.stdout != expected + '\n':
                    raise RuntimeError(f'{name}: {result.returncode} {result.stdout} {result.stderr}')
                print(f'{name} RED (rc=1)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
