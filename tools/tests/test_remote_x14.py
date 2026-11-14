#!/usr/bin/env python3
"""Serial command injection, including EOT and echo failure handling."""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch
import io

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'tools/rshell_serial.py'
CHECK = dict(name='remote-x14', tier=1, needs=[], budget=10,
             covers=['tools/rshell_serial.py', 'tools/tests/test_remote_x14.py',
                     'tools/host_image.py', 'tools/lan_bridge.py', 'tools/lanfs_host.py',
                     'tools/rshell_hwb.py', 'tools/rshell_peek.py', 'tools/serialfs_host.py'])


def load(path):
    spec = importlib.util.spec_from_file_location('rshell_x14', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Port:
    def __init__(self, response):
        self.response = response
        self.sent = b''
        self.resets = 0
        self.flushed = False

    def reset_input_buffer(self):
        self.resets += 1

    def write(self, data):
        self.sent += data

    def flush(self):
        self.flushed = True

    @property
    def in_waiting(self):
        return len(self.response)

    def read(self, size):
        chunk, self.response = self.response[:size], self.response[size:]
        return chunk


def exercise(module):
    port = Port(b'> ver\nbody\n\x04')
    assert module.run_serial(port, 'ver', 0.01) == (0, '> ver\nbody\n')
    assert port.sent == b'ver\n' and port.resets == 1 and port.flushed
    assert module.run_serial(Port(b'> version\n\x04'), 'ver', 0.01)[0] == 1
    assert module.run_serial(Port(b'> ver\n'), 'ver', 0.001)[0] == 1
    assert module.run_serial(Port(b'\x04'), 'exit', 0.01) == (0, '')
    port = Port(b'> ver\nbody\n\x04')
    output = io.StringIO()
    with patch.object(module, 'run_serial', wraps=module.run_serial) as injected:
        assert module.run_line(SimpleNamespace(timeout=0.01), port, 'ver',
                               out=output) == (0, '> ver\nbody\n')
        assert injected.call_count == 1 and output.getvalue() == '> ver\nbody\n'


def main():
    exercise(load(SRC))
    print('remote-x14 PASS', flush=True)
    if '--mutate' in sys.argv and os.environ.get('MUTATE', '1') != '0':
        source = SRC.read_text(encoding='utf-8')
        pattern = r'    if not check_echo\(text, line\):\n        if out is not None:'
        mutated, count = re.subn(pattern, '    if False:\n        if out is not None:', source)
        assert count == 1, 'mutation anchor must be unique'
        with tempfile.TemporaryDirectory(prefix='remote-x14-') as tmp:
            path = Path(tmp) / 'mutant.py'
            path.write_text(mutated, encoding='utf-8')
            try:
                exercise(load(path))
            except AssertionError:
                print('MUTATION echo omitted RED', flush=True)
            else:
                raise AssertionError('mutation survived')


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
