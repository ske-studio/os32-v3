#!/usr/bin/env python3
"""Exercise the operator client against the real socket adapter."""
import argparse
import importlib.util
import io
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile
import threading
import time
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from host_operator_socket import OperatorSocket

CHECK = dict(name='remote-x13', tier=1, needs=[], budget=10,
             covers=['tools/remote_transport.py', 'tools/host_agent_config.py', 'tools/host_operator_socket.py',
                     'tools/tests/test_remote_x13.py', 'tools/rshell.py', 'tools/gserial.py',
                     'tools/host_image.py', 'tools/lan_bridge.py', 'tools/lanfs_host.py',
                     'tools/rshell_hwb.py', 'tools/rshell_peek.py', 'tools/rshell_serial.py',
                     'tools/serialfs_host.py'])


class FakeOperator:
    generation = 7
    session = 1
    ready = dict(mode='cui', last_id='none', state='idle')
    pending = None
    def __init__(self):
        self.notices = [b'gs:! hello']
        self.results = {}
        self.disconnect = False
        self.slow = False
        self.server = None
    def submit(self, mode, line, client):
        self.take_result()
        self.pending = dict(id='00000001', state='running' if self.slow else 'done',
                            result=None if self.slow else b'> ver\nOK\n\x04')
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
        operator.slow = True
        assert client.request('cui', 'ver', 0.1)['state'] == 'unknown', 'CLI deadline not UNKNOWN'
        assert client.sock is None
        assert operator.pending['state'] == 'running', 'CLI deadline changed agent'
        operator.pending.update(state='done', result=b'> ver\nlate\n\x04')
        assert client.result('00000001', 0, 1)['complete'], 'late result after CLI deadline lost'
        operator.slow = False
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


def test_auto(path):
    spec = importlib.util.spec_from_file_location('remote_matrix', path)
    transport = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(transport)
    transport.READY_TIMEOUT = 0.001
    import rshell
    import gserial

    for requested in ('cui', 'gui'):
        for current in (requested, 'gui' if requested == 'cui' else 'cui'):
            for state in ('idle', 'running', 'unknown'):
                for absent, hello, has_ready in ((False, True, True), (True, True, True),
                                                 (False, False, True), (False, True, False),
                                                 (False, False, False)):
                    calls = []
                    class Client(transport.CommandTransport):
                        def _call(self, op, deadline, **fields):
                            calls.append(op)
                            if absent:
                                raise OSError('agent absent')
                            if op == 'probe':
                                return dict(ok=True, hello=hello, ready=dict(mode=current) if has_ready else None,
                                            pending=None if state == 'idle' else dict(state=state))
                            if op == 'submit':
                                return dict(ok=True, id='1')
                            return dict(ok=True, state='done', complete=True, drop=0,
                                        body_b64='Z3M6MDAwMDAwMDEgZW5kIDAK')
                    serial_calls = []
                    def port_factory(*args):
                        serial_calls.append(1)
                        raise OSError('serial selected')
                    client = Client()
                    result = client.request(requested, 'ping', 1)
                    if requested == 'gui':
                        cli = gserial.Client(None, 1, stdout=io.StringIO(), stderr=io.StringIO(),
                                             lan=Client(), transport='auto', port_factory=port_factory)
                        try:
                            cli.request('ping')
                        except (ValueError, OSError):
                            pass
                    else:
                        with patch.object(rshell, 'CommandTransport', return_value=Client()), \
                             patch.object(rshell, 'serial_command', side_effect=port_factory), \
                             patch.object(sys, 'stdout', io.StringIO()), patch.object(sys, 'stderr', io.StringIO()):
                            try:
                                rshell.main(['cmd', 'ping'])
                            except OSError:
                                pass
                            except AttributeError:
                                # Successful rshell output uses stdout.buffer.
                                pass
                    fallback = absent or (state == 'idle' and (not hello or not has_ready))
                    marker = ('missing hello did not call port_factory' if not hello and state == 'idle'
                              else 'pending priority called port_factory')
                    assert bool(serial_calls) == fallback, marker
                    if fallback:
                        assert result.get('state') == 'serial'
                    elif state != 'idle':
                        assert result.get('error') == ('BUSY' if state == 'running' else 'UNKNOWN_PENDING')
                    elif current != requested:
                        assert f'mode={current}' in result['error'] and f'requested={requested}' in result['error']
                    else:
                        assert result.get('state') == 'done'
                    assert ('submit' in calls) == (not absent and hello and has_ready and
                                                  state == 'idle' and current == requested)
    print('PASS auto matrix')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/remote_transport.py'))
    args = parser.parse_args()
    test_auto(args.module)
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [
            ("if not probe.get('hello') or not ready:", "if not ready:", 'missing hello did not call port_factory'),
            ("return {'ok': False, 'error': error}", "return {'state': 'serial'}", 'pending priority called port_factory'),
            ("return {'state': 'unknown'}", "return {'state': 'serial'}", 'CLI deadline not UNKNOWN'),
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
