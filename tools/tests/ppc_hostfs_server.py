#!/usr/bin/env python3
"""Exercise PLN1 boot and the real SerialFS server."""
CHECK = dict(name='ppc-hostfs-server', tier=1, budget=30, needs=[],
             covers=['tools/ppc_hostfs.py', 'tools/ppc_lan_host.py'],
             inputs=['tools/tests/ppc_hostfs_server.py', 'tools/lanfs_host.py', 'tools/serialfs_host.py'])
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT / 'tools'))
import ppc_lan_host as lan
import serialfs_host as sfs


class Port:
    local_mac = b'local!'
    def __init__(self):
        self.sent = []
    def send(self, frame):
        self.sent.append(frame)


def exercise():
    with tempfile.TemporaryDirectory() as tmp:
        port = Port()
        peer, boot, new = b'peer!!', b'a' * 16, b'b' * 16
        host = lan.Host(port, peer, tmp, output=lambda _: None, root=tmp, allow_write=['allowed'])
        def send(kind, ident=boot, **kwargs):
            host.receive(port.local_mac + peer + struct.pack('!H', lan.ETHERTYPE) + lan.encode(kind, ident, **kwargs))
        def request(data, ident=boot):
            before = len(port.sent)
            send(lan.FS_REQ, ident, nonce=b'x' * 16, payload=data)
            return lan.decode(port.sent[-1][14:]) if len(port.sent) > before else None
        send(lan.BEACON, seq=1)
        assert lan.decode(port.sent[-1][14:])['kind'] == lan.FS_OPEN
        hello = sfs.encode(sfs.T_HELLO, 0, 0, struct.pack('<HHI', 1, 1028, 42))
        first = request(hello)
        assert first['nonce'] == host.fs_nonce and request(hello)['payload'] == first['payload']
        parsed = sfs.try_frame(first['payload'], 0, 1028)[2]
        assert struct.unpack_from('<H', parsed.payload, 6)[0] == 1028
        sid = parsed.sid
        def write(seq, name, data):
            path = name.encode()
            return sfs.encode(sfs.T_WRITE, sid, seq, struct.pack('<IBB', 0, sfs.WF_TRUNC, len(path)) + path + data, 1028)
        w1 = write(1, '/allowed', b'first')
        assert request(w1)
        assert request(write(2, '/allowed', b'second'))
        assert request(w1) is None
        assert (pathlib.Path(tmp) / 'allowed').read_bytes() == b'second'
        assert request(write(3, '/denied', b'bad'))
        assert not (pathlib.Path(tmp) / 'denied').exists()
        old_server = host.fs.server
        send(lan.BEACON, seq=0)
        assert host.fs.server is not old_server
        old_nonce = host.fs_nonce
        send(lan.BEACON, new)
        assert host.boot == new and host.fs_nonce != old_nonce
        assert request(hello) is None
        send(lan.BEACON, boot)
        assert host.boot == new
        assert request(hello, new)
        assert (pathlib.Path(tmp) / 'allowed').read_bytes() == b'second'
    print('PASS')


def main():
    if '--child' in sys.argv:
        exercise()
        return 0
    exercise()
    if '--mutate' in sys.argv:
        for name, filename, old, new in [
            ('boot', 'ppc_lan_host.py', "if m['kind'] != BEACON or boot in self.retired:", "if True:"),
            ('duplicate', 'lanfs_host.py', 'elif fr.seq <= self.high_seq:', 'elif False:')]:
            with tempfile.TemporaryDirectory() as tmp:
                for path in (ROOT / 'tools').glob('*.py'):
                    shutil.copy(path, tmp)
                path = pathlib.Path(tmp) / filename
                text = path.read_text()
                assert text.count(old) == 1
                path.write_text(text.replace(old, new))
                code = "import sys; sys.path.insert(0, sys.argv[1]); sys.path.insert(1, sys.argv[2]); import ppc_hostfs_server as t; t.exercise()"
                result = subprocess.run([sys.executable, '-B', '-c', code, tmp, str(ROOT / 'tools/tests')], capture_output=True, text=True)
                assert result.returncode != 0 and 'AssertionError' in result.stderr, (name, result.stderr)
                print(name + ' RED')
    return 0


def check(ctx):
    return subprocess.call([sys.executable, '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
