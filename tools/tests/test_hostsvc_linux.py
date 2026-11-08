#!/usr/bin/env python3
"""Exercise Linux host services with isolated executable stand-ins."""
CHECK = dict(name='hostsvc-linux', tier=1, needs=[],
             covers=['tools/host_linux.py'], budget=3.3)

import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/host_linux.py'
MUTATION = ("    if path is None:\n        raise ServiceUnavailable('missing executable: ' + name)",
            "    if path is None:\n        return '/bin/true'")


def load(path):
    spec = importlib.util.spec_from_file_location('host_linux_tested', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def finish(proc):
    deadline = time.monotonic() + 5
    while proc.poll() is None:
        if time.monotonic() >= deadline:
            proc.kill()
            raise AssertionError('child timed out')
        time.sleep(0.01)
    result = (proc.poll(), proc.output(), proc.stderr())
    proc.close()
    return result


def test(module, work):
    bindir = work / 'bin'
    bindir.mkdir()
    record = work / 'record'
    script = '#!' + sys.executable + '''
import os, pathlib, sys
p = pathlib.Path(os.environ['RECORD'])
p.write_bytes(('\\n'.join(sys.argv[1:]) + '\\n').encode() + sys.stdin.buffer.read())
if pathlib.Path(sys.argv[0]).name in ('wl-paste', 'xclip') and ('-out' in sys.argv or pathlib.Path(sys.argv[0]).name == 'wl-paste'):
    sys.stdout.buffer.write('日本語\\n'.encode('utf-8'))
'''
    env = {'PATH': str(bindir), 'RECORD': str(record)}
    with patch.dict(os.environ, env, clear=True):
        def unavailable(fn):
            try:
                proc = fn()
            except module.ServiceUnavailable:
                return
            finish(proc)
            raise AssertionError('missing dependency must map to 503')

        unavailable(lambda: module.clip_start('get', b'', 'auto'))
        unavailable(lambda: module.print_start(b'', None, False))
        os.environ['DISPLAY'] = ':99'
        unavailable(lambda: module.clip_start('put', b'x', 'auto'))
        for name in ('xclip', 'wl-copy', 'wl-paste', 'lp'):
            executable = bindir / name
            executable.write_text(script)
            executable.chmod(0o755)
        payload = '日本語\n'.encode('utf-8')
        assert finish(module.clip_start('put', payload, 'auto'))[0] == 0
        assert record.read_bytes() == b'-selection\nclipboard\n-in\n' + payload
        assert finish(module.clip_start('get', b'', 'auto'))[:2] == (0, payload)
        os.environ['WAYLAND_DISPLAY'] = 'wayland-1'
        assert finish(module.clip_start('put', payload, 'auto'))[0] == 0
        assert record.read_bytes() == b'\n' + payload
        assert finish(module.clip_start('get', b'', 'auto'))[:2] == (0, payload)
        assert record.read_bytes() == b'--no-newline\n'
        assert finish(module.print_start(payload, 'printer name', True))[0] == 0
        assert record.read_bytes() == b'-d\nprinter name\n-o\nraw\n' + payload
        assert finish(module.print_start(payload, None, False))[0] == 0
        assert record.read_bytes() == b'\n' + payload
        (bindir / 'lp').unlink()
        unavailable(lambda: module.print_start(payload, None, False))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='hostsvc-linux-') as tmp:
        work = Path(tmp)
        test(load(SOURCE), work)
        print('PASS Linux host services')
        if args.mutate:
            source = SOURCE.read_text()
            old, new = MUTATION
            if source.count(old) != 1:
                raise RuntimeError('mutation anchor is not unique')
            mutated = work / 'mutated.py'
            mutated.write_text(source.replace(old, new))
            subwork = work / 'mutation'
            subwork.mkdir()
            try:
                test(load(mutated), subwork)
            except AssertionError as exc:
                print('RED missing executable:', exc)
            else:
                raise RuntimeError('mutation survived')
    return 0


def run(ctx):
    argv = [sys.executable, '-B', str(Path(__file__).resolve())]
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
