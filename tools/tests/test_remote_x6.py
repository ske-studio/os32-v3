#!/usr/bin/env python3
"""Exercise the host link/operator integration and its failure guards."""
import argparse
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
CHECK = dict(name='remote-x6', tier=1, needs=[], budget=5,
             covers=['tools/host_agent_config.py', 'tools/host_agent.py', 'tools/host_linux.py', 'tools/host_notify.py',
                     'tools/host_services.py', 'tools/host_rshell.py',
                     'tools/host_operator_socket.py', 'tools/tests/test_remote_x6.py'])


def test(path):
    spec = importlib.util.spec_from_file_location('agent_x6', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    agent = mod.HostAgent(b'\x02\0\0\0\0\1', quiet=True)
    sink = mod.ListSink()
    agent._switch_to(dict(sess=1, epoch=1, mac=b'\x02\0\0\0\0\2'))
    boot = 'a' * 32
    def request(rid, line, body=None):
        agent._on_request(rid, 0, line.encode(), sink)
        if body:
            agent._on_wdata(rid, 1, body, sink)
        ent = agent.ledger[rid]
        return ent
    ready = request(1, 'RSH READY %s cui 1 none idle' % boot)
    assert ready['resp'][0] == 200 and ready['body'] == (
        'gen %d svc 65534\n' % agent.host_generation).encode()
    agent._on_release(1, sink)
    ident = agent.operator.submit('cui', 'ver', None)
    assert agent.notifier.pending[-1][4:6] == (65534).to_bytes(2, 'little')
    body = b'> ver\nOK\n\x04'
    output = request(2, 'RSH OUT %s %s 0 %d' % (boot, ident, len(body)), body)
    assert output['resp'][0] == 200 and agent.operator.take_result()['result'] == body
    agent._on_release(2, sink)
    class Subscriber:
        def publish_log(self, record):
            self.record = record
    sub = Subscriber()
    agent.operator_socket = sub
    record = request(3, 'LOG %s 1 0 0 0 3' % boot, b'abc')
    assert record['resp'][0] == 200 and sub.record['text'] == b'abc'
    agent._on_release(3, sink)
    bad = dict(req=b'', got=b'abc')
    agent._service_log(4, bad, ['LOG', boot, '1', '0', '0', '0', '2'], sink)
    assert bad['resp'][0] == 400, 'length guard removed'
    agent.operator.pending = dict(state='running')
    agent._switch_to(dict(sess=2, epoch=1, mac=b'\x02\0\0\0\0\2'))
    assert agent.operator.pending['state'] == 'UNKNOWN', 'reset removed'
    # Configured peer selects the guest for HELLO, before allocating a session.
    peer = b'\x02\0\0\0\0\2'
    agent = mod.HostAgent(b'\x02\0\0\0\0\1', quiet=True, peer=peer)
    sink = mod.ListSink()
    agent._on_hello(b'\x02\0\0\0\0\3', mod.HS_SYN, 1, 10, 0, 0, b'', sink)
    assert agent.cand is None and not sink.frames, 'wrong peer HELLO accepted'
    agent._on_hello(peer, mod.HS_SYN, 1, 10, 0, 0, b'', sink)
    assert agent.cand is not None and sink.frames, 'configured peer HELLO rejected'
    candidate = agent.cand.copy()
    agent._on_hello(b'\x02\0\0\0\0\3', mod.HS_CONFIRM, 1, 10,
                    candidate['agent_nonce'], candidate['sess'], b'', sink)
    assert not agent.established and agent.cand == candidate, 'wrong peer CONFIRM accepted'
    print('PASS remote-x6')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/host_agent.py'))
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [('if self.configured_peer is not None and src != self.configured_peer:',
                      'if False:', 'wrong peer HELLO accepted'),
                     ('int(parts[6]) != len(ent["got"])', 'False', 'length guard removed'),
                     ('        self.operator.session_reset()\n        self.sess,',
                      '        self.sess,', 'reset removed')]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'host_agent.py'
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
