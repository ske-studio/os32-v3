#!/usr/bin/env python3
"""Test numbered notifications against the existing wire contract."""
import argparse
import importlib.util
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='remote-x3', tier=1, needs=[], budget=3,
             covers=['tools/host_notify.py', 'include/remote_control.h',
                     'net/link.h', 'tools/tests/test_remote_x3.py'])


def test(path):
    spec = importlib.util.spec_from_file_location('host_notify_x3', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    header = (ROOT / 'include/remote_control.h').read_text()
    link = (ROOT / 'net/link.h').read_text()
    def value(text, name):
        return int(re.search(r'^#define\s+' + name + r'\s+(\d+)', text, re.M).group(1))
    service = value(header, 'REMOTE_SVC_RSHELL')
    limit = value(link, 'LINK_NOTIFY_DATA_MAX')
    assert '#define REMOTE_NOTIFY_DATA_MAX    LINK_NOTIFY_DATA_MAX' in header
    class Registry:
        def resolve(self, name):
            assert name == 'reserved-test'
            return service
    for kind_name in ('REMOTE_NOTIFY_EXECUTE', 'REMOTE_NOTIFY_PROBE'):
        kind = value(header, kind_name)
        data = bytes(range(limit))
        numbered = module.Notify(Registry(), 0x12345678)
        named = module.Notify(Registry(), 0x12345678)
        numbered.notify_number(service, kind, data)
        named.notify('reserved-test', kind, data)
        expected = struct.pack('<IHHHH', 0x12345678, service, kind, limit, 0) + data
        assert numbered.pending[0] == named.pending[0] == expected
        try:
            numbered.notify_number(service, kind, data + b'x')
        except ValueError:
            pass
        else:
            raise AssertionError('oversized notification accepted')
        assert len(numbered.pending) == 1
    print('PASS remote-x3')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/host_notify.py'))
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        old = ' or len(data) > DATA_MAX'
        assert source.count(old) == 1
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'host_notify.py'
            path.write_text(source.replace(old, ''))
            result = subprocess.run([sys.executable, '-B', __file__, '--module', str(path)],
                                    capture_output=True, text=True)
            assert result.returncode != 0 and 'oversized notification accepted' in result.stderr
            print('RED length check (runtime)')


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
