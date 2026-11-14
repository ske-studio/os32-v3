#!/usr/bin/env python3
"""Host tests for the rshell CLI and its transport boundary."""
import argparse
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
CHECK = dict(name='remote-x12', tier=1, needs=[], budget=10,
             covers=['tools/rshell.py', 'tools/remote_transport.py',
                     'tools/rshell_serial.py', 'tools/rshell_peek.py',
                     'tools/rshell_hwb.py', 'tools/serialfs_host.py',
                     'tools/lanfs_host.py', 'tools/lan_bridge.py',
                     'tools/host_image.py', 'tools/tests/test_remote_x12.py'])


class Output:
    def __init__(self):
        self.buffer = io.BytesIO()
    def write(self, text):
        self.buffer.write(text.encode())
    def flush(self):
        pass


class FakeTransport:
    result = None
    calls = []
    def request(self, mode, line, timeout):
        self.calls.append(('request', mode, line))
        return self.result
    def resume(self, timeout):
        self.calls.append(('resume',))
        return {'ok': True}
    def probe(self, timeout):
        self.calls.append(('probe',))
        return {'ok': True, 'hello': True}
    def close(self):
        pass


def test(path):
    spec = importlib.util.spec_from_file_location('rshell_x12', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    fake = FakeTransport()
    serial_calls = []
    def serial(args, line):
        serial_calls.append(line)
        return 0
    out = Output()
    with patch.object(mod, 'CommandTransport', return_value=fake), \
         patch.object(mod, 'serial_command', side_effect=serial), \
         patch.object(sys, 'stdout', out), patch.object(sys, 'stderr', io.StringIO()):
        fake.result = {'state': 'serial'}
        assert mod.main(['cmd', 'ver']) == 0 and serial_calls == ['ver']
        for tail in (['ls', '-l', '/usr/bin'], ['--', 'ls', '-l', '/usr/bin'],
                     ['echo', '--timeout', '3']):
            try:
                assert mod.main(['cmd', *tail]) == 0
            except SystemExit:
                raise AssertionError('command options consumed')
            expected = ' '.join(tail[1:] if tail[0] == '--' else tail)
            assert serial_calls[-1] == expected, 'command options consumed'
        serial_calls[:] = ['ver']
        fake.result = {'state': 'unknown'}
        try:
            rc = mod.main(['cmd', 'ver'])
        except SystemExit:
            raise AssertionError('command options consumed')
        assert serial_calls == ['ver'], 'submit fell back to serial'
        assert rc != 0, 'unknown returned success'
        fake.result = {'ok': True, 'state': 'done', 'complete': True,
                       'body_b64': 'PiB2ZXINCk9LDQoE'}
        assert mod.main(['cmd', 'ver']) == 0
        assert out.buffer.getvalue() == b'OK\r\n'
        fake.result['complete'] = False
        assert mod.main(['cmd', 'ver']) != 0
        assert mod.main(['--resume']) == 0 and fake.calls[-1] == ('resume',)
        assert mod.main(['--probe']) == 0 and fake.calls[-1] == ('probe',)
        assert mod.main(['--transport', 'lan', 'cmd', '']) == 0
    print('PASS remote-x12')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/rshell.py'))
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [
            ('nargs=argparse.REMAINDER', "nargs='*'", 'command options consumed'),
            ("print('communication incomplete: ' + str(result.get('state', result.get('error', 'error'))), file=sys.stderr)\n            return 1",
             "print('communication incomplete: ' + str(result.get('state', result.get('error', 'error'))), file=sys.stderr)\n            return 0", 'unknown returned success'),
            ("result.get('state') == 'serial' and args.transport == 'auto'",
             "result.get('state') in ('serial', 'unknown') and args.transport == 'auto'", 'submit fell back to serial'),
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'rshell.py'
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
