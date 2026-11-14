#!/usr/bin/env python3
"""Exercise the operator client against the real socket adapter."""
import argparse
import importlib.util
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from host_operator_socket import OperatorSocket

CHECK = dict(name='remote-x13', tier=1, needs=[], budget=10,
             covers=['tools/remote_transport.py', 'tools/host_agent_config.py', 'tools/host_operator_socket.py',
                     'tools/tests/test_remote_x13.py'])


class FakeOperator:
    generation = 7
    session = 1
    ready = dict(mode='cui', last_id='none', state='idle')
    pending = None
    def __init__(self):
        self.notices = [b'gs:! hello']
        self.results = {}
        self.disconnect = False
        self.server = None
    def submit(self, mode, line, client):
        self.take_result()
        self.pending = dict(id='00000001', state='done', result=b'> ver\nOK\n\x04')
        if self.disconnect:
            client['socket'].close()
        return '00000001'
    def take_result(self):
        if self.pending is not None and self.pending['state'] in ('done', 'truncated'):
            self.results[self.pending['id']] = self.pending
            result = self.pending
            self.pending = None
            return result
    def resume(self):
        self.pending = None


def test(path):
    spec = importlib.util.spec_from_file_location('remote_transport', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    operator = FakeOperator()
    info = lambda: dict(commit='abc', config=dict(iface='eth0', root='/',
                        allow_write=False, peer=None), peer=None)
    server = OperatorSocket.__new__(OperatorSocket)
    server.operator, server.info = operator, info
    server.listener, listener_peer = socket.socketpair()
    server.clients = {}
    class SocketAdapter:
        def __init__(self, sock):
            self.sock = sock
        def fileno(self):
            return self.sock.fileno()
        def recv(self, size):
            return os.read(self.fileno(), size)
        def send(self, data):
            return os.write(self.fileno(), data)
        def close(self):
            self.sock.close()
    def connect(addr, timeout):
        client, peer = socket.socketpair()
        peer.setblocking(False)
        server.clients[peer.fileno()] = dict(socket=peer, data=bytearray(),
                                             out=bytearray(), wait=None, log=False)
        return SocketAdapter(client)
    mod.socket.create_connection = connect
    stop = threading.Event()
    def pump():
        while not stop.is_set():
            for fd in select.select(server.fileno_list(), [], [], 0.01)[0]:
                server.on_readable(fd)
            server.poll(time.monotonic())
    thread = threading.Thread(target=pump)
    thread.start()
    client = mod.CommandTransport()
    try:
        assert client.probe()['commit'] == 'abc'
        assert client.notice(0, 1)['notices'][0]['id'] == '!'
        assert client.subscribe_log()['ok']
        server.publish_log(dict(boot='a', seq=1, drop_records=0, drop_bytes=0,
                                flags=0, text=b'log'))
        assert client.probe()['ok']
        assert client.next_log(1)['event'] == 'log'
        result = client.request('cui', 'ver', 2)
        assert result['state'] == 'done' and result['complete']
        ident = client.submit('cui', 'ver', 1)['id']
        assert client.result(ident, 0, 1)['state'] == 'done'
        assert client.resume()['ok']
        operator.ready = None
        start = time.monotonic()
        assert client.request('cui', 'ver', 5)['state'] == 'serial'
        assert 3.8 <= time.monotonic() - start < 4.7, 'ready deadline removed'
        operator.ready = dict(mode='cui', last_id='none', state='idle')
        operator.disconnect = True
        assert client.request('cui', 'ver', 2)['state'] == 'unknown', 'submit fell back to serial'
    finally:
        client.close()
        stop.set()
        thread.join()
        server.close()
        listener_peer.close()
    print('PASS remote-x13')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/remote_transport.py'))
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [
            ("return {'state': 'unknown'}", "return {'state': 'serial'}", 'submit fell back to serial'),
            ('time.monotonic() + READY_TIMEOUT)', 'time.monotonic() + READY_TIMEOUT + 1)', 'ready deadline removed'),
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'remote_transport.py'
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
