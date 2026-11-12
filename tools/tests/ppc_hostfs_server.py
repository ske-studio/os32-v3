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


def require(condition, diagnostic, code):
    if not condition:
        print(diagnostic, file=sys.stderr)
        raise SystemExit(code)


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
        def frame(response):
            assert response and response['kind'] == lan.FS_RSP and response['nonce'] == host.fs_nonce
            parsed = sfs.try_frame(response['payload'], 0, 1028)
            assert parsed[0] == 'frame' and parsed[1] == len(response['payload'])
            return parsed[2]
        send(lan.BEACON, seq=1)
        assert lan.decode(port.sent[-1][14:])['kind'] == lan.FS_OPEN
        hello = sfs.encode(sfs.T_HELLO, 0, 0, struct.pack('<HHI', 1, 1028, 42))
        first = request(hello)
        parsed = frame(first)
        assert struct.unpack_from('<H', parsed.payload, 6)[0] == 1028
        sid = parsed.sid
        seq = 0
        path = b'/allowed'
        def rpc(kind, payload):
            nonlocal seq
            seq += 1
            wire = sfs.encode(kind, sid, seq, payload, 1028)
            reply = frame(request(wire))
            assert reply.type == kind | sfs.T_RESP and reply.sid == sid and reply.seq == seq
            return wire, reply.payload
        def write(offset, flags, data):
            return rpc(sfs.T_WRITE, struct.pack('<IBB', offset, flags, len(path)) + path + data)
        w1, reply = write(0, sfs.WF_TRUNC, b'first')
        assert struct.unpack('<i', reply)[0] == 5
        print('REACHED nonce', flush=True)
        repeated = request(hello)
        require(repeated == first and host.fs.server.sid == sid and host.fs.adapter.high_seq == 1,
                'nonce replay lost session or guard', 21)
        assert request(w1) and host.fs.server.executed == 1
        _, reply = write(0, sfs.WF_TRUNC, b'second')
        assert struct.unpack('<i', reply)[0] == 6
        print('REACHED duplicate', flush=True)
        require(request(w1) is None and (pathlib.Path(tmp) / 'allowed').read_bytes() == b'second',
                'old duplicate executed', 22)
        data = bytes(range(256)) * 256
        for offset in range(0, len(data), 1000):
            chunk = data[offset:offset + 1000]
            _, reply = write(offset, sfs.WF_TRUNC if offset == 0 else 0, chunk)
            assert struct.unpack('<i', reply)[0] == len(chunk)
        _, reply = write(17, 0, b'short')
        assert struct.unpack('<i', reply)[0] == 5
        data = data[:17] + b'short' + data[22:]
        _, reply = write(len(data), 0, b'append')
        assert struct.unpack('<i', reply)[0] == 6
        data += b'append'
        assert (pathlib.Path(tmp) / 'allowed').read_bytes() == data
        readback = bytearray()
        for offset in range(0, len(data), 1000):
            _, reply = rpc(sfs.T_READ, struct.pack('<IHB', offset, 1000, len(path)) + path)
            count = struct.unpack_from('<i', reply)[0]
            assert count == len(reply) - 4
            readback.extend(reply[4:])
        assert readback == data
        _, reply = rpc(sfs.T_LIST, struct.pack('<IB', 0, 1) + b'/')
        assert struct.unpack_from('<iI', reply) == (0, 0)
        entries, pos = {}, 8
        while pos < len(reply):
            kind, size, length = struct.unpack_from('<BIB', reply, pos)
            pos += 6
            entries[reply[pos:pos + length]] = (kind, size)
            pos += length
        assert entries[b'allowed'][1] == len(data)
        denied = b'/denied'
        _, reply = rpc(sfs.T_WRITE, struct.pack('<IBB', 0, sfs.WF_TRUNC, len(denied)) + denied + b'bad')
        assert struct.unpack('<i', reply)[0] == sfs.ERR_ROFS
        assert not (pathlib.Path(tmp) / 'denied').exists()
        # R2-1 is independent of the later boot transition.
        old_server = host.fs.server
        send(lan.BEACON, seq=0)
        assert host.fs.server is not old_server
        sid = frame(request(hello)).sid
        seq = 0
        _, reply = write(0, 0, b'active')
        assert struct.unpack('<i', reply)[0] == 6
        terminal = lan.Terminal(host)
        host.terminal = terminal
        terminal.connected, terminal.in_seq, terminal.out_next = True, 3, 4
        host.request('status')
        send(lan.LOG, seq=2, payload=b'log')
        log_path = host.storage(boot) / (boot.hex() + '.log')
        old_server, old_adapter = host.fs.server, host.fs.adapter
        old_nonce, old_term_nonce = host.fs_nonce, host.nonce
        fs, transport = host.fs.fs, host.io
        saved = (pathlib.Path(tmp) / 'allowed').read_bytes()
        print('REACHED boot', flush=True)
        send(lan.BEACON, new)
        require(host.boot == new and host.fs.server is not old_server and
                host.fs.adapter is not old_adapter and host.fs.server.sid == 0 and
                not host.fs.server.closed and host.fs.server.cache_key is None and
                host.fs.adapter.high_seq == 0 and host.fs.adapter.nonce is None and
                host.fs.adapter.last_request is None and host.fs.adapter.last_response is None,
                'boot failed to reset FS session', 23)
        assert host.fs_nonce != old_nonce and host.nonce != old_term_nonce
        assert not terminal.connected and terminal.in_seq == terminal.out_next == 0
        assert not host.pending and host.next_id == 1
        assert host.high is None and host.max_next_seq == 0 and not host.seen and host.generation == 0
        assert host.fs.fs is fs and host.io is transport and host.peer == peer and boot in host.retired
        assert (pathlib.Path(tmp) / 'allowed').read_bytes() == saved
        assert log_path.read_bytes() == b'log'
        assert any(lan.decode(f[14:])['kind'] == lan.TERM_OPEN for f in port.sent)
        print('REACHED retired', flush=True)
        require(request(hello) is None, 'retired boot accepted', 24)
        send(lan.BEACON, boot)
        require(host.boot == new, 'retired boot accepted', 24)
        assert frame(request(hello, new)).sid
    print('PASS')


def main():
    exercise()
    if '--mutate' in sys.argv:
        for name, filename, old, new, marker, diagnostic, code in [
            ('boot', 'ppc_lan_host.py', "if m['kind'] != BEACON or boot in self.retired:", "if False:", 'REACHED retired', 'retired boot accepted', 24),
            ('duplicate', 'lanfs_host.py', 'elif fr.seq <= self.high_seq:', 'elif False:', 'REACHED duplicate', 'old duplicate executed', 22),
            ('reset', 'ppc_lan_host.py', '            self.fs.reset()\n        if self.terminal is not None:', '            pass\n        if self.terminal is not None:', 'REACHED boot', 'boot failed to reset FS session', 23),
            ('nonce', 'lanfs_host.py', 'if self.server.sid and src == self.peer and nonce == self.nonce:', 'if False:', 'REACHED nonce', 'nonce replay lost session or guard', 21)]:
            with tempfile.TemporaryDirectory() as tmp:
                for path in (ROOT / 'tools').glob('*.py'):
                    shutil.copy(path, tmp)
                path = pathlib.Path(tmp) / filename
                text = path.read_text()
                assert text.count(old) == 1
                path.write_text(text.replace(old, new))
                command = "import sys; sys.path.insert(0, sys.argv[1]); sys.path.insert(1, sys.argv[2]); import ppc_hostfs_server as t; t.exercise()"
                result = subprocess.run([sys.executable, '-B', '-c', command, tmp, str(ROOT / 'tools/tests')], capture_output=True, text=True)
                assert result.returncode == code and marker in result.stdout.splitlines() and result.stderr.strip() == diagnostic, (name, result.returncode, result.stdout, result.stderr)
                print(name + ' RED')
    return 0

def check(ctx):
    return subprocess.call([sys.executable, '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
