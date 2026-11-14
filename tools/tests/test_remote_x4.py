#!/usr/bin/env python3
"""Contract tests for the generation-local LAN operator."""
import argparse
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='remote-x4', tier=1, needs=[], budget=3,
             covers=['tools/host_rshell.py', 'tools/host_notify.py',
                     'include/remote_control.h', 'tools/tests/test_remote_x4.py'])
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
    assert op.accept_ready(42, 2, BOOT, 'cui', ident, 'done')
    assert op.take_result() is None
    assert output(session=2)
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
    op.unknown()
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
    assert op.accept_output(42, 3, BOOT, next_id, 5, body, len(body), 'cui'), 'drop rejected'
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
            ("# previous ID/idle state is not evidence that execution was lost.",
             "# previous ID/idle state is not evidence that execution was lost.\n        if self.pending is not None and last_id != self.pending['id']:\n            self.pending['state'] = 'UNKNOWN'", 'stale READY lost operation'),
            ("self.take_result()  # Retain an unclaimed completion before reusing the slot.",
             "pass", 'busy'),
            ('if dropped < 0 or length != len(body) or length > RESULT_MAX:',
             'if dropped != 0 or length != len(body) or length > RESULT_MAX:', 'drop rejected'),
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
