#!/usr/bin/env python3
"""LOG client ordering, CLI output and configured operator endpoints."""
import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
CHECK = dict(name='remote-u5e', tier=1, needs=[], budget=10,
             covers=['tools/remote_transport.py', 'tools/host_agent_config.py',
                     'tools/systemd/host-agent.example.json',
                     'tools/tests/test_remote_u5e.py'])


def test(path):
    spec = importlib.util.spec_from_file_location('u5e_transport', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    config = json.loads((ROOT / 'tools/systemd/host-agent.example.json').read_text())
    config['operator-endpoint'] = '127.0.0.2:9123'
    with tempfile.TemporaryDirectory() as directory:
        config_path = Path(directory) / 'nic.json'
        config_path.write_text(json.dumps(config))
        with patch.object(mod.os.path, 'join', return_value=str(config_path)):
            client = mod.CommandTransport(instance='nic')
        assert (client.host, client.port) == ('127.0.0.2', 9123)
    assert (mod.CommandTransport(endpoint='localhost:8029').port) == 8029
    for kwargs in (dict(endpoint=':1'), dict(instance='../nic'),
                   dict(endpoint='host:2', instance='nic')):
        try:
            mod.CommandTransport(**kwargs)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid target accepted')

    client_socket, server_socket = socket.socketpair()
    class Adapter:
        def fileno(self):
            return client_socket.fileno()
        def send(self, data):
            return os.write(self.fileno(), data)
        def recv(self, size):
            return os.read(self.fileno(), size)
        def close(self):
            client_socket.close()
    events = [dict(event='log', boot='a', seq=n, drop_records=n,
                   drop_bytes=n * 3, flags=0, text_b64='eA==') for n in (1, 2)]
    def wire(obj):
        data = json.dumps(obj).encode()
        return struct.pack('!I', len(data)) + data
    def serve():
        conn = server_socket
        with conn:
            def read(n):
                data = b''
                while len(data) < n:
                    chunk = os.read(conn.fileno(), n - len(data))
                    if not chunk:
                        return data
                    data += chunk
                return data
            request = json.loads(read(struct.unpack('!I', read(4))[0]))
            assert request == {'op': 'log'}
            os.write(conn.fileno(), wire(events[0]) + wire(events[1]) + wire({'ok': True}))
    thread = threading.Thread(target=serve)
    thread.start()
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out), patch.object(mod.socket, 'create_connection', return_value=Adapter()):
            try:
                mod.main(['--endpoint', '127.0.0.1:8027', 'log'])
            except ConnectionError:
                pass
    finally:
        thread.join()
    lines = [json.loads(line) for line in out.getvalue().splitlines()]
    assert [line['seq'] for line in lines] == [1, 2], 'LOG order changed'
    assert [(line.get('drop_records'), line.get('drop_bytes')) for line in lines] == [(1, 3), (2, 6)], 'drop omitted'
    print('PASS remote-u5e')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/remote_transport.py'))
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [
            ('self.events.append(obj)', 'self.events.insert(0, obj)', 'LOG order changed'),
            ('print(json.dumps(event, ensure_ascii=True), flush=True)',
             "print(json.dumps({k: v for k, v in event.items() if not k.startswith('drop_')}, ensure_ascii=True), flush=True)", 'drop omitted'),
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
