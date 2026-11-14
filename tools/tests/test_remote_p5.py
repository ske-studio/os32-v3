#!/usr/bin/env python3
"""P5 client contract, LOG persistence and loss reporting."""
import argparse
import base64
import contextlib
import io
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import ppc_lan_host

CHECK = dict(name='remote-p5', tier=1, needs=[], budget=10,
             covers=['tools/host_agent_config.py', 'tools/host_image.py',
                     'tools/host_linux.py', 'tools/host_notify.py', 'tools/host_rshell.py',
                     'tools/host_services.py', 'tools/lan_bridge.py', 'tools/lanfs_host.py',
                     'tools/ppc_hostfs.py', 'tools/serialfs_host.py',
                     'tools/ppc_link_transport.py', 'tools/ppc_lan_host.py',
                     'tools/remote_transport.py', 'tools/host_operator_socket.py',
                     'tools/host_agent.py', 'tools/tests/test_remote_p5.py'])


class Agent:
    """JSON-operation fake; only fields from the operator contract are used."""
    def __init__(self, *args):
        self.events = []
        self.calls = []
    def close(self):
        pass
    def subscribe_log(self, timeout):
        self.calls.append('log')
        return dict(ok=True)
    def next_log(self, timeout):
        if not self.events:
            raise TimeoutError('operator timeout')
        return self.events.pop(0)
    def submit(self, mode, line, timeout):
        self.calls.append((mode, line))
        return dict(ok=True, id='00000001')
    def result(self, ident, wait_ms, timeout):
        assert ident == '00000001'
        return dict(ok=True, state='done', drop=0, complete=True,
                    body_b64=base64.b64encode(b'diagnostic').decode())


def test(path):
    spec = importlib.util.spec_from_file_location('p5', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.CommandTransport = Agent
    output = []
    host = module.Host(output=output.append)
    host.subscribe_log()
    assert host.transport.calls == ['log']
    boot = '01' * 16
    for seq in (0, 3):
        host.transport.events.append(dict(event='log', boot=boot, seq=seq,
            drop_records=7, drop_bytes=29, flags=0,
            text_b64=base64.b64encode(b'hello\n').decode()))
    with tempfile.TemporaryDirectory() as directory:
        display = io.StringIO()
        with contextlib.redirect_stdout(display):
            for _ in range(2):
                record = host.receive()
                ppc_lan_host.save_agent_log(Path(directory), record)
        assert display.getvalue() == 'hello\nhello\n'
        assert (Path(directory) / (boot + '.log')).read_bytes() == b'hello\nhello\n'
        import json
        rows = [json.loads(line) for line in (Path(directory) / (boot + '.jsonl')).read_text().splitlines()]
        assert [r['seq'] for r in rows] == [0, 3]
        assert rows[1]['drop_records'] == 7
    assert 'gap: 2' in output, 'gap missed'
    assert any('drop_records=7 drop_bytes=29' in line for line in output), 'drop hidden'
    assert host.boot == bytes.fromhex(boot)
    other = dict(record, boot='02' * 16, seq=0)
    host.transport.events.append(other)
    host.receive()
    assert output.count('gap: 2') == 1
    try:
        host.receive(.01)
    except TimeoutError:
        pass
    else:
        raise AssertionError('missing timeout')
    result = host.request('status')
    assert base64.b64decode(result['body_b64']) == b'diagnostic'
    assert ('diag', 'status') in host.transport.calls
    host.send_line('ver')
    assert ('cui', 'ver') in host.transport.calls
    host.transport.result = lambda *args: dict(ok=True, state='pending')
    try:
        host.result('00000001', .001)
    except TimeoutError:
        pass
    else:
        raise AssertionError('result missing timeout')
    print('PASS remote-p5')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/ppc_link_transport.py'))
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [('if seq > previous + 1:', 'if False:', 'gap missed'),
                     ("records, size = event['drop_records'], event['drop_bytes']",
                      'records, size = 0, 0', 'drop hidden')]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'p5.py'
            for old, new, marker in mutations:
                assert source.count(old) == 1
                path.write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, '-B', __file__, '--module', str(path)], capture_output=True, text=True)
                assert result.returncode and marker in result.stderr, result.stderr
                print('RED', marker)


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
