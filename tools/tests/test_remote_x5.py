#!/usr/bin/env python3
"""Socket contract and no-replay tests for the LAN operator adapter."""
import argparse
import base64
import importlib.util
import json
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from host_rshell import Operator

CHECK = dict(name='remote-x5', tier=1, needs=[], budget=3,
             covers=['tools/host_operator_socket.py', 'tools/host_rshell.py',
                     'tools/tests/test_remote_x5.py'])


def test(path):
    spec = importlib.util.spec_from_file_location('remote_x5', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    class Notify:
        generation = 7
        def __init__(self):
            self.sent = []
        def notify_number(self, *args):
            self.sent.append(args)
    notify = Notify()
    operator = Operator(notify)
    boot = 'a' * 32
    operator.session_start(1)
    assert operator.accept_ready(7, 1, boot, 'cui', 'none', 'idle')
    info = lambda: dict(commit='abc', config=dict(iface='eth0', root='/',
                        allow_write=False, peer=None), peer=None)
    server = mod.OperatorSocket.__new__(mod.OperatorSocket)
    server.operator, server.info = operator, info
    server.listener, listener_peer = socket.socketpair()
    server.clients = {}
    def connect():
        client, peer = socket.socketpair()
        peer.setblocking(False)
        server.clients[peer.fileno()] = dict(socket=peer, data=bytearray(),
                                             out=bytearray(), wait=None, log=False)
        client.settimeout(1)
        return client
    sock = connect()
    def pump():
        for fd in select.select(server.fileno_list(), [], [], 0)[0]:
            server.on_readable(fd)
        server.poll(time.monotonic())
    def send(obj):
        data = json.dumps(obj).encode()
        os.write(sock.fileno(), struct.pack('!I', len(data)) + data)
        pump()
    def receive():
        pump()
        def read(n):
            data = b''
            while len(data) < n:
                data += os.read(sock.fileno(), n - len(data))
            return data
        return json.loads(read(struct.unpack('!I', read(4))[0]))
    pump()
    send(dict(op='probe'))
    assert receive()['commit'] == 'abc'
    send(dict(op='submit', mode='cui', line='ver'))
    ident = receive()['id']
    send(dict(op='result', id=ident, wait_ms=1000))
    body = b'> ver\nOK\n\x04'
    assert operator.accept_output(7, 1, boot, ident, 0, body, len(body), 'cui')
    result = receive()
    assert result['complete'] and base64.b64decode(result['body_b64']) == body
    operator.notices.append(b'gs:! ready\n')
    send(dict(op='notice', wait_ms=0))
    assert receive()['notices'][0]['id'] == '!'
    send(dict(op='log'))
    assert receive()['ok']
    server.publish_log(dict(boot=boot, seq=1, drop_records=0, drop_bytes=0, flags=0, text=b'log'))
    assert base64.b64decode(receive()['text_b64']) == b'log'
    send(dict(op='submit', mode='cui', line='again'))
    ident = receive()['id']
    sock.close()
    pump()
    assert len(notify.sent) == 2, 'disconnect replayed operation'
    # A nonblocking result query does not expire an operation.
    sock = connect()
    send(dict(op='result', id=ident, wait_ms=0))
    assert receive()['state'] == 'pending'
    # Expiry must survive a CLI disconnect just before its result deadline.
    send(dict(op='result', id=ident, wait_ms=1000))
    deadline = time.monotonic() + 2
    sock.close()
    pump()
    server.poll(deadline)
    assert operator.pending['state'] == 'UNKNOWN', 'result timeout left running'
    assert operator.ready is None, 'timeout reused old READY'
    sock = connect()
    pump()
    send(dict(op='submit', mode='cui', line='again'))
    assert receive()['error'] == 'UNKNOWN_PENDING'
    assert operator.accept_ready(7, 1, boot, 'cui', ident, 'done')
    send(dict(op='resume'))
    assert receive()['ok']
    # A connected long poll returns UNKNOWN at its deadline. A fresh READY
    # then allows the same ID's delayed OUT, without executing again.
    send(dict(op='submit', mode='cui', line='ver'))
    ident = receive()['id']
    send(dict(op='result', id=ident, wait_ms=1000))
    server.poll(time.monotonic() + 2)
    assert receive()['state'] == 'unknown', 'result deadline response pending'
    assert operator.accept_ready(7, 1, boot, 'cui', ident, 'done')
    assert operator.accept_output(7, 1, boot, ident, 0, body, len(body), 'cui')
    send(dict(op='result', id=ident, wait_ms=0))
    assert receive()['complete'] and len(notify.sent) == 3
    os.write(sock.fileno(), struct.pack('!I', 1) + b'{')
    assert receive()['error'] == 'BAD_REQUEST'
    os.write(sock.fileno(), struct.pack('!I', mod.RECORD_MAX + 1))
    assert receive()['error'] == 'BAD_REQUEST', 'oversize accepted'
    sock.close()
    server.close()
    listener_peer.close()
    print('PASS remote-x5')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/host_operator_socket.py'))
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [
            ("now >= pending.get('result_deadline', float('inf'))", 'False',
             'result timeout left running'),
            ('if size > RECORD_MAX:', 'if size > RECORD_MAX + 1:', 'oversize accepted'),
            ("client['socket'].close()", "client['socket'].close()\n            if self.operator.pending:\n                self.operator.notify.notify_number(0, 0, b'replay')", 'disconnect replayed operation'),
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'host_operator_socket.py'
            for old, new, marker in mutations:
                assert source.count(old) == 1
                path.write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, '-B', __file__, '--module', str(path)],
                                        capture_output=True, text=True)
                assert result.returncode and marker in result.stderr, result.stderr
                print('RED', marker)


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
