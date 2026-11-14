#!/usr/bin/env python3
"""PDIAG notification encoding and result delivery, without sockets."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
CHECK = dict(name='remote-p2h', tier=1, needs=[], budget=5,
             covers=['tools/host_agent.py', 'tools/host_rshell.py',
                     'tools/host_agent_config.py', 'tools/host_linux.py',
                     'tools/host_notify.py', 'tools/host_services.py',
                     'tools/remote_transport.py',
                     'tools/host_operator_socket.py', 'tools/ppc_link_transport.py',
                     'tools/tests/test_remote_p2h.py'],
    basis=['LAN_OPERATOR_DESIGN §5'],
)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test(agent_path, operator_path):
    operator = load('host_rshell', operator_path)
    sys.modules['host_rshell'] = operator
    mod = load('agent_p2h', agent_path)
    agent = mod.HostAgent(bytes.fromhex('020000000001'), quiet=True)
    sink = mod.ListSink()
    agent._switch_to(dict(sess=1, epoch=1, mac=bytes.fromhex('020000000002')))
    boot = 'a' * 32
    rid = 0
    def request(line, body=None):
        nonlocal rid
        rid += 1
        agent._on_request(rid, 0, line.encode(), sink)
        if body is not None:
            agent._on_wdata(rid, 1, body, sink)
        entry = agent.ledger[rid]
        agent._on_release(rid, sink)
        return entry
    assert request(f'RSH READY {boot} diag 1 none idle')['resp'][0] == 200
    sent = []
    agent.operator.notify.notify_number = lambda *args: sent.append(args)
    from ppc_link_transport import Host
    class Transport:
        def submit(self, mode, line, timeout):
            return dict(ok=True, id=agent.operator.submit(mode, line, None))
        def result(self, ident, wait_ms, timeout):
            return dict(ok=True, state=agent.operator.pending['state'])
    host = Host.__new__(Host)
    host.transport = Transport()
    for command, encoded in [('status', b'1'), ('clock', b'2'),
                             ('memory', b'3'), ('read-diag 0x12 3', b'4 00120003'),
                             ('peek 0x12345678 256', b'5 1234567801000000')]:
        ident = agent.operator.submit('diag', command, None)
        assert sent[-1] == (65532, 1, ident.encode() + b' ' + encoded + b'\n'), 'diag BE encoding'
        count = len(sent)
        try:
            agent.operator.submit('diag', 'status', None)
        except RuntimeError:
            pass
        else:
            raise AssertionError('busy accepted')
        assert len(sent) == count
        assert agent.operator.take_result() is None
        body = b'\x00diagnostic\xff'
        for wrong_boot, wrong_id in [('b' * 32, ident), (boot, 'ffffffff')]:
            assert request(f'PDIAG OUT {wrong_boot} {wrong_id} 0 {len(body)}', body)['resp'][0] == 400
        assert request(f'PDIAG OUT {boot} {ident} 0 1', body)['resp'][0] == 400, 'PDIAG overflow'
        bad = dict(req=b'', got=body)
        accept = agent.operator.accept_output
        agent.operator.accept_output = lambda *args: True
        agent._service_rshell(999, bad, ['PDIAG', 'OUT', boot, ident, '0', str(len(body) + 1)], sink)
        agent.operator.accept_output = accept
        assert bad['resp'][0] == 400, 'PDIAG length guard'
        agent.operator.expire(agent.operator.pending['deadline'])
        assert agent.operator.pending['state'] == 'UNKNOWN'
        agent.operator.session_reset()
        agent.operator.session_start((1, 1))
        assert request(f'PDIAG OUT {boot} {ident} 3 {len(body)}', body)['resp'][0] == 200
        result = agent.operator.take_result()
        assert result['result'] == body and result['status'] == 3 and result['state'] == 'done'
        assert request(f'RSH READY {boot} diag 1 {ident} done')['resp'][0] == 200
    for command in ['status x', 'read-diag 255 2', 'read-diag 0 0', 'peek 0xffffffff 2', 'peek 0 257', 'unknown']:
        count = len(sent)
        try:
            agent.operator.submit('diag', command, None)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid diagnostic accepted')
        assert len(sent) == count and agent.operator.pending is None
    assert request(f'PDIAG BAD {boot} 00000001 0 1', b'x')['resp'][0] == 400
    # Host.request waits for a complete result through its existing transport.
    def result(ident, wait_ms, timeout):
        assert agent.operator.pending['state'] == 'running'
        assert request(f'PDIAG OUT {boot} {ident} 0 2', b'OK')['resp'][0] == 200
        return dict(ok=True, state=agent.operator.take_result()['state'])
    host.transport.result = result
    assert host.request('status')['state'] == 'done'
    # A refused diagnostic carries a status and no body: zero declared length, no WDATA.
    def refused(ident, wait_ms, timeout):
        assert request(f'PDIAG OUT {boot} {ident} 3 0')['resp'][0] == 200, 'zero-length PDIAG OUT'
        taken = agent.operator.take_result()
        assert taken['status'] == 3 and taken['result'] == b''
        return dict(ok=True, state=taken['state'])
    host.transport.result = refused
    assert host.request('peek 0x100000 1')['state'] == 'done'
    print('PASS remote-p2h')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--agent', default=str(ROOT / 'tools/host_agent.py'))
    parser.add_argument('--operator', default=str(ROOT / 'tools/host_rshell.py'))
    args = parser.parse_args()
    test(args.agent, args.operator)
    if args.mutate:
        for path, flag, old, new, marker in [
            (args.agent, '--agent', 'if length == len(ent["got"]) and self.operator.accept_output(',
             'if self.operator.accept_output(', 'PDIAG length guard'),
            (args.operator, '--operator', "struct.pack('!HH', offset, length)",
             "struct.pack('<HH', offset, length)", 'diag BE encoding')]:
            source = Path(path).read_text()
            assert source.count(old) == 1
            with tempfile.TemporaryDirectory() as tmp:
                mutant = Path(tmp) / Path(path).name
                mutant.write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, '-B', __file__, flag, str(mutant)], capture_output=True, text=True)
                assert result.returncode and marker in result.stderr, result.stderr
                print('RED', marker)


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
