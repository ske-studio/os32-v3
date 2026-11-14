#!/usr/bin/env python3
"""Exercise the operator while the guest transport is blocked opening."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import socket
import select
import struct
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/tests'))
from host_operator_socket import OperatorSocket
from test_host_agent import FakeOS32, HA

CHECK = dict(name='remote-u5d', tier=1, needs=[], budget=10,
             covers=['tools/host_agent.py', 'tools/host_agent_config.py',
                     'tools/host_linux.py', 'tools/host_notify.py',
                     'tools/host_operator_socket.py', 'tools/host_rshell.py',
                     'tools/host_services.py', 'tools/tests/test_host_agent.py',
                     'tools/tests/test_remote_u5d.py'])


def request(sock, obj):
    data = json.dumps(obj).encode()
    os.write(sock.fileno(), struct.pack('!I', len(data)) + data)
    def read(n):
        out = b''
        while len(out) < n:
            if not select.select([sock], [], [], 1)[0]:
                raise TimeoutError()
            part = os.read(sock.fileno(), n - len(out))
            if not part:
                raise EOFError()
            out += part
        return out
    return json.loads(read(struct.unpack('!I', read(4))[0]))


def test(path):
    spec = importlib.util.spec_from_file_location('agent_u5d', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    agent = mod.HostAgent(bytes.fromhex('02005e000001'), quiet=True)
    client, server = socket.socketpair()
    listener, listener_peer = socket.socketpair()
    client.settimeout(1)
    server.setblocking(False)
    listener.setblocking(False)
    operator = OperatorSocket.__new__(OperatorSocket)
    operator.operator = agent.operator
    operator.info = lambda: dict(commit='', config={}, peer=None)
    operator.listener = listener
    operator.clients = {server.fileno(): dict(socket=server, data=bytearray(),
                        out=bytearray(), wait=None, log=False)}
    gate = threading.Event()
    stream = object()
    mod.open_stream = lambda args: (gate.wait(), stream)[1]
    result = []
    worker = threading.Thread(target=lambda: result.append(mod.wait_for_stream(None, operator)))
    worker.start()
    try:
        try:
            probe = request(client, {'op': 'probe'})
        except (OSError, EOFError):
            raise AssertionError('operator stopped while waiting for guest')
        assert probe['ok'] and not probe['hello'] and probe['ready'] is None
        assert request(client, {'op': 'submit', 'mode': 'cui', 'line': 'ver'})['error'] == 'NOT_READY'
        assert request(client, {'op': 'result', 'id': '00000001', 'wait_ms': 0})['error'] == 'NO_SUCH_ID'
        assert request(client, {'op': 'notice', 'wait_ms': 20})['notices'] == []
        assert request(client, {'op': 'log'})['ok']
        gate.set()
        worker.join(2)
        assert result == [stream]
        guest = FakeOS32(agent)
        assert guest.hello()[1]['flags'] == HA.HS_ESTAB
        # Service the real socket adapter as the connected loop does.
        def exchange(obj):
            os.write(client.fileno(), struct.pack('!I', len(json.dumps(obj).encode())) + json.dumps(obj).encode())
            operator.on_readable(server.fileno())
            header = os.read(client.fileno(), 4)
            size = struct.unpack('!I', header)[0]
            return json.loads(os.read(client.fileno(), size))
        probe = exchange({'op': 'probe'})
        assert probe['hello'] and probe['ready'] is None
        assert exchange({'op': 'submit', 'mode': 'cui', 'line': 'ver'})['error'] == 'NOT_READY'
        ready = ('RSH READY %s cui 1 none idle' % ('a' * 32)).encode()
        response = guest.send(HA.OP_REQUEST, rid=1, payload=ready)
        assert any(p['op'] == HA.OP_RESPONSE for p in response)
        assert exchange({'op': 'probe'})['ready']['mode'] == 'cui'
        assert exchange({'op': 'submit', 'mode': 'cui', 'line': 'ver'})['ok']
        agent.operator.session_reset()
        assert not exchange({'op': 'probe'})['hello']
    finally:
        gate.set()
        worker.join(2)
        operator.close()
        listener_peer.close()
        client.close()
    print('PASS remote-u5d')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/host_agent.py'))
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        old = '            service_operator(operator_socket, TICK_INTERVAL)'
        assert source.count(old) == 1
        with tempfile.TemporaryDirectory() as tmp:
            mutant = Path(tmp) / 'host_agent.py'
            mutant.write_text(source.replace(old, '            time.sleep(TICK_INTERVAL)'))
            result = subprocess.run([sys.executable, '-B', __file__, '--module', str(mutant)],
                                    capture_output=True, text=True)
            assert result.returncode and 'operator stopped while waiting for guest' in result.stderr, result.stderr
            print('RED operator stopped while waiting for guest')


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
