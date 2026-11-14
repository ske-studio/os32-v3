#!/usr/bin/env python3
"""Contract tests for the generation-local LAN operator."""
import argparse
import json
import os
import socket
import struct
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='remote-x4', tier=1, needs=[], budget=3,
             covers=['tools/host_rshell.py', 'tools/host_notify.py',
                     'tools/host_operator_socket.py', 'include/remote_control.h', 'tools/tests/test_remote_x4.py'])
BOOT = '0123456789abcdef0123456789abcdef'


def test(path):
    spec = importlib.util.spec_from_file_location('remote_x4', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    header = (ROOT / 'include/remote_control.h').read_text()
    for name, value in [('REMOTE_SVC_RSHELL', mod.RSHELL),
                        ('REMOTE_SVC_GSERIAL', mod.GSERIAL),
                        ('REMOTE_NOTIFY_EXECUTE', mod.EXECUTE),
                        ('REMOTE_RESULT_BODY_MAX', mod.RESULT_MAX)]:
        assert int(re.search(r'^#define\s+' + name + r'\s+(\d+)', header, re.M)[1]) == value
    class Notify:
        generation = 42
        def __init__(self):
            self.sent = []
        def notify_number(self, *args):
            self.sent.append(args)
    n = Notify()
    op = mod.Operator(n)
    op.session_start(1)
    assert op.accept_ready(42, 1, BOOT, 'cui', 'none', 'idle')
    ident = op.submit('cui', 'ver', object())
    assert ident == '00000001' and n.sent == [(mod.RSHELL, 1, b'00000001 ver\n')]
    try:
        op.submit('cui', 'ver', None)
    except RuntimeError:
        pass
    assert len(n.sent) == 1, 'busy sent'
    body = b'> ver\r\nversion\n\x04'
    def output(gen=42, session=1, boot=BOOT, ident=ident, drop=0, data=body, length=None, mode='cui'):
        return op.accept_output(gen, session, boot, ident, drop, data,
                                len(data) if length is None else length, mode)
    assert not output(gen=41), 'old generation accepted'
    assert not output(boot='f' * 32)
    assert not output(ident='00000002')
    assert not output(length=len(body) + 1)
    assert not output(data=b'> version\n\x04')
    assert not output(data=body[:-1]), 'missing EOT accepted'
    op.session_reset()
    assert op.pending['state'] == 'UNKNOWN'
    op.session_start(2)
    assert op.ready is None
    assert op.take_result() is None
    assert not output(gen=41, session=2), 'old generation accepted'
    assert not output(session=1), 'old session accepted'
    assert not output(session=2, boot='f' * 32), 'wrong boot accepted'
    assert not output(session=2, ident='00000002'), 'wrong id accepted'
    assert output(session=2), 'late OUT without READY rejected'
    assert op.take_result()['result'] == body
    assert op.accept_ready(42, 2, BOOT, 'gui', ident, 'done')
    ident2 = op.submit('gui', 'ping', None)
    assert n.sent[-1] == (mod.GSERIAL, 1, b'\x0100000002 ping\n')
    assert op.accept_output(42, 2, BOOT, '!', 0, b'gs:! ready\n', 11, 'gui')
    assert op.pending['id'] == ident2
    gs = b'gs:00000002 end 0\n'
    assert not op.accept_output(42, 2, BOOT, ident2, 0, b'gs:00000002 ok\n', 15, 'gui')
    assert op.accept_output(42, 2, BOOT, ident2, 0, gs, len(gs), 'gui')
    assert op.take_result()['state'] == 'done'
    op.next_id = 0xffffffff
    op.submit('gui', 'ping', None)
    ready = op.ready
    op.unknown()
    assert op.ready is ready, 'UNKNOWN cleared READY'
    assert op.accept_ready(42, 2, BOOT, 'gui', 'ffffffff', 'done')
    op.resume()
    try:
        op.submit('gui', 'ping', None)
    except RuntimeError:
        pass
    else:
        raise AssertionError('id wrapped')
    # Periodic READY reconciles pending work but never fabricates a result.
    for last_id, state in [('none', 'idle'), ('00000001', 'done'),
                           ('00000000', 'done')]:
        n = Notify()
        op = mod.Operator(n)
        op.session_start(1)
        assert op.accept_ready(42, 1, BOOT, 'cui', 'none', 'idle')
        ident = op.submit('cui', 'ver', None)
        assert op.accept_ready(42, 1, BOOT, 'cui', ident, 'running')
        assert op.pending['state'] == 'running'
        assert op.accept_ready(42, 1, BOOT, 'cui', last_id, state)
        assert op.pending['state'] == 'running', 'stale READY lost operation'
        assert op.take_result() is None and len(n.sent) == 1
        assert op.accept_output(42, 1, BOOT, ident, 0, body, len(body), 'cui')
        assert op.take_result()['result'] == body
        op.submit('cui', 'ver', None)
        assert op.accept_ready(42, 1, BOOT, 'cui', 'none', 'idle')
        assert op.pending['state'] == 'running', 'stale READY lost operation'
        assert op.accept_ready(42, 1, 'f' * 32, 'cui', 'none', 'idle')
        assert op.pending['state'] == 'UNKNOWN'
        assert not op.accept_output(42, 1, BOOT, op.pending['id'], 0, body, len(body), 'cui'), 'old boot accepted after restart'
        assert op.accept_ready(42, 1, BOOT, 'cui', 'none', 'idle')
        op.resume()
        assert op.pending is None and len(n.sent) == 2
    # An OUT after the CLI left is retained while the next submission proceeds.
    ident = op.submit('cui', 'ver', None)
    assert op.accept_ready(42, 1, BOOT, 'cui', 'none', 'idle')
    assert op.pending['state'] == 'running', 'stale READY lost operation'
    assert op.accept_output(42, 1, BOOT, ident, 0, body, len(body), 'cui')
    op.session_reset()
    op.session_start(3)
    assert op.accept_ready(42, 3, 'f' * 32, 'cui', 'none', 'idle')
    assert op.pending['state'] == 'done', 'completed result became UNKNOWN'
    assert op.accept_ready(42, 3, BOOT, 'cui', 'none', 'idle')
    next_id = op.submit('cui', 'ver', None)
    assert op.results[ident]['result'] == body, 'unclaimed result lost'
    assert op.pending['id'] == next_id
    body = b'output tail\n\x04'
    assert not op.accept_output(42, 3, BOOT, next_id, 0, body, len(body), 'cui'), 'missing echo accepted'
    assert not op.accept_output(42, 3, BOOT, next_id, 5, body[:-1], len(body) - 1, 'cui'), 'missing EOT accepted'
    assert op.accept_output(42, 3, BOOT, next_id, 5, body, len(body), 'cui'), 'drop without echo rejected'
    assert op.pending is None and op.results[next_id]['state'] == 'truncated', 'drop not finalized'
    assert op.results[next_id]['drop'] == 5
    exit_id = op.submit('cui', 'exit', None)
    assert op.accept_output(42, 3, BOOT, exit_id, 0, b'\x04', 1, 'cui')
    try:
        op.submit('cui', 'ver', None)
    except RuntimeError as exc:
        assert str(exc) == 'not ready', 'exit left READY'
    else:
        raise AssertionError('exit left READY')
    assert op.results[exit_id]['state'] == 'done'
    assert op.accept_ready(42, 3, BOOT, 'cui', exit_id, 'done')
    op.submit('cui', 'ver', None)
    assert op.accept_output(42, 3, BOOT, '!', 33, b'gs:! ready v1\r\n', 15, 'gui'), 'dropped notice rejected'
    assert op.notice_drop == 33, 'notice drop not recorded'
    assert op.notices[-1] == b'gs:! ready v1\r\n'
    for data in (b'gs:! partial', b'gs:! ready v1\ngs:! extra\n'):
        assert not op.accept_output(42, 3, BOOT, '!', 1, data, len(data), 'gui'), 'partial notice accepted'
    # Operation timeout is independent of result polling and client lifetime.
    body = b'> ver\r\nversion\n\x04'
    sys.path.insert(0, str(ROOT / 'tools'))
    from host_operator_socket import OperatorSocket
    n = Notify()
    op = mod.Operator(n)
    op.session_start(1)
    assert op.accept_ready(42, 1, BOOT, 'cui', 'none', 'idle')
    server = OperatorSocket.__new__(OperatorSocket)
    server.operator = op
    server.info = lambda: dict(commit='x', config={}, peer=None)
    sock, peer = socket.socketpair()
    client = dict(socket=sock, out=bytearray(), wait=None)
    server.clients = {sock.fileno(): client}
    ident = op.submit('cui', 'ver', client)
    deadline = op.pending['deadline']
    server._disconnect(sock.fileno())
    assert op.pending['state'] == 'running', 'disconnect released operation'
    op.expire(deadline - 0.001)
    assert op.pending['state'] == 'running', 'early expiry'
    server.poll(deadline)
    assert op.pending['state'] == 'UNKNOWN', 'deadline did not become UNKNOWN'
    probe = dict(out=bytearray(), wait=None)
    server._request(probe, dict(op='probe'))
    assert json.loads(probe['out'][4:])['pending'] == dict(id=ident, state='unknown')
    assert op.accept_output(42, 1, BOOT, ident, 0, body, len(body), 'cui')
    assert op.take_result()['result'] == body, 'late result rejected'
    ident = op.submit('cui', 'ver', None)
    server.poll(op.pending['deadline'])
    assert op.pending['state'] == 'UNKNOWN', 'deadline did not become UNKNOWN'
    server._request(probe, dict(op='resume'))
    assert op.pending is None
    assert op.submit('cui', 'ver', None) != ident, 'resume did not reopen slot'
    peer.close()
    # Exercise wire validation and the operation deadline with a fixed clock.
    server.listener, listener_peer = socket.socketpair()
    sock, peer = socket.socketpair()
    client = dict(socket=sock, data=bytearray(), out=bytearray(), wait=None)
    server.clients = {sock.fileno(): client}
    def request(req):
        data = json.dumps(req).encode()
        os.write(peer.fileno(), struct.pack('!I', len(data)) + data)
        server.on_readable(sock.fileno())
        size = struct.unpack('!I', os.read(peer.fileno(), 4))[0]
        return json.loads(os.read(peer.fileno(), size))
    try:
        with patch.object(mod.time, 'monotonic', return_value=100):
            op.pending = None
            for fields, seconds in [({'timeout_ms': 60000}, 60), ({}, 15)]:
                response = request(dict(op='submit', mode='cui', line='ver', **fields))
                assert response['ok']
                assert op.pending['deadline'] == 100 + seconds, 'timeout_ms ignored'
                server.poll(115 if seconds == 60 else 114.999)
                assert op.pending['state'] == 'running', 'timeout_ms ignored'
                server.poll(100 + seconds)
                assert request(dict(op='probe'))['pending']['state'] == 'unknown'
                assert request(dict(op='resume'))['ok']
            sent = len(n.sent)
            for value in (0, -1, True, False, 1.5, '60000', None, [], {}, 10**400):
                response = request(dict(op='submit', mode='cui', line='ver', timeout_ms=value))
                assert response['error'] == 'BAD_REQUEST', 'invalid timeout accepted'
            assert op.pending is None and len(n.sent) == sent, 'invalid timeout sent'
    finally:
        server.close()
        peer.close()
        listener_peer.close()
    print('PASS remote-x4')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/host_rshell.py'))
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [
            ('deadline = time.monotonic() + timeout_ms / 1000',
             'deadline = time.monotonic() + RESULT_TIMEOUT_MS / 1000', 'timeout_ms ignored'),
            ('            self.unknown()\n\n    def unknown', '            pass\n\n    def unknown', 'deadline did not become UNKNOWN'),
            ("if (not body.startswith(b'gs:! ')", "if (dropped or not body.startswith(b'gs:! ')", 'dropped notice rejected'),
            ("self.pending['state'] = 'UNKNOWN'\n\n    def resume", "self.pending['state'] = 'UNKNOWN'\n            self.ready = None\n\n    def resume", 'UNKNOWN cleared READY'),
            ('(self.ready is None and not late)', '(self.ready is None)', 'late OUT without READY rejected'),
            ('self.notice_drop += dropped', 'pass', 'notice drop not recorded'),
            ("# previous ID/idle state is not evidence that execution was lost.",
             "# previous ID/idle state is not evidence that execution was lost.\n        if self.pending is not None and last_id != self.pending['id']:\n            self.pending['state'] = 'UNKNOWN'", 'stale READY lost operation'),
            ("self.take_result()  # Retain an unclaimed completion before reusing the slot.",
             "pass", 'busy'),
            ('if dropped < 0 or length != len(body) or length > RESULT_MAX:',
             'if dropped != 0 or length != len(body) or length > RESULT_MAX:', 'drop without echo rejected'),
            ("if not dropped and pending['line'] != 'exit'", "if pending['line'] != 'exit'", 'drop without echo rejected'),
            ("if mode == 'cui' and pending['line'] == 'exit':", 'if False:', 'exit left READY'),
            ("if self.pending is not None:\n            raise RuntimeError('busy')", "if False:\n            raise RuntimeError('busy')", 'busy sent'),
            ('generation != self.generation or session != self.session or session is None or', 'False or session != self.session or session is None or', 'old generation accepted'),
            ("if not body.endswith(b'\\x04'):", "if False:", 'missing EOT accepted'),
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'host_rshell.py'
            for old, new, marker in mutations:
                assert source.count(old) == 1
                path.write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, '-B', __file__, '--module', str(path)], capture_output=True, text=True)
                assert result.returncode != 0 and marker in result.stderr, result.stderr
                print('RED', marker)


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
