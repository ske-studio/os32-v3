#!/usr/bin/env python3
"""H7: generation-local ids, LIST origins and dispatch, with runtime mutants."""
CHECK = dict(name='hostsvc-services', tier=1, needs=[], budget=2,
             covers=['tools/host_services.py', 'tools/hostsvc_contracts.pyi',
                     'tools/tests/hostsvc_fixture.py'])
import argparse
import importlib.util
import logging
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def test(path):
    spec = importlib.util.spec_from_file_location('host_services', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    registry = module.Registry(0x12345678)
    expected = {'os32.alive': 'builtin', 'os32.clip': 'builtin',
                'os32.print': 'builtin', 'os32.http': 'builtin',
                'os32.file': 'sfs', 'os32.iso9660': 'sfs',
                'os32.udf': 'stub', 'os32.mail': 'stub',
                'os32.feed': 'stub', 'os32.chat': 'stub'}
    lines = registry.list_bytes().decode('utf-8').splitlines()
    assert lines.pop(0) == 'generation 305419896'
    rows = [line.split() for line in lines]
    assert {name: origin for name, version, number, origin in rows} == expected
    assert len(rows) == len(expected)
    ids = [int(row[2]) for row in rows]
    assert len(set(ids)) == len(ids) and all(1 <= i <= 65534 for i in ids)
    for name, version, number, origin in rows:
        assert version == '1' and registry.resolve(name) == int(number)
        assert registry.dispatch(name + ' GET', b'body') == (501, b'')
    assert registry.dispatch('LIST', b'') == (200, registry.list_bytes())
    assert registry.dispatch('unknown GET', b'') == (404, b'')
    assert registry.dispatch('', b'') == (404, b'')
    try:
        registry.resolve('unknown')
    except KeyError:
        pass
    else:
        raise AssertionError('unknown resolve did not raise KeyError')
    for name in ('', 'UPPER', 'with space', 'a/b', '日本語', 'x\n'):
        try:
            registry.register(name, 1, 'plugin', None)
        except ValueError:
            pass
        else:
            raise AssertionError('accepted invalid name: %r' % name)
    calls = []

    def handler(line, data):
        calls.append((line, data))
        return 201, b'output'

    number = registry.register('example.echo', 2, 'plugin', handler)
    assert registry.dispatch('example.echo PUT args', b'input') == (201, b'output')
    assert calls == [('example.echo PUT args', b'input')]
    with patch.object(module.LOG, 'warning') as warning:
        assert registry.register('example.echo', 3, 'plugin', None) == number
        assert warning.call_count == 1
    assert registry.dispatch('example.echo PUT', b'') == (501, b'')
    assert registry.list_bytes().count(b'example.echo ') == 1
    assert ('example.echo 3 %d plugin\n' % number).encode() in registry.list_bytes()
    with patch.object(module.LOG, 'warning') as warning:
        reserved = registry.register('os32.custom', 1, 'plugin', handler)
        assert warning.call_count == 1
    assert registry.resolve('os32.custom') == reserved
    assert registry.dispatch('os32.custom GET', b'') == (201, b'output')
    # Exercise the real 16-bit boundary, including replacement at exhaustion.
    for i in range(65534 - len(rows) - 2):
        last = registry.register('test.%d' % i, 1, 'plugin', None)
    assert last == 65534
    with patch.object(module.LOG, 'warning'):
        assert registry.register('example.echo', 4, 'plugin', handler) == number
    try:
        registry.register('overflow', 1, 'plugin', None)
    except RuntimeError:
        pass
    else:
        raise AssertionError('service id overflow accepted')
    assert module.Registry(2).list_bytes().startswith(b'generation 2\n')
    print('PASS hostsvc-services')


MUTATIONS = [
    ('duplicate id', 'service_id = previous[1]', 'service_id = len(self._services) + 1'),
    ('id exhaustion', 'if service_id > MAX_SERVICE_ID:', 'if False:'),
    ('unknown status', "return 404, b''", "return 501, b''"),
    ('stub status', "return 501, b''", "return 404, b''"),
    ('SFS origin', "('sfs', SFS_SERVICES)", "('stub', SFS_SERVICES)"),
    ('name validation', "if re.fullmatch(r'[a-z0-9.]+', name) is None:", 'if False:'),
    ('duplicate warning', "LOG.warning('duplicate service %s: last registration wins', name)", 'pass'),
    ('reserved warning', "if origin == 'plugin' and name.startswith('os32.'):", 'if False:'),
    ('handler input', 'return handler(line, data)', "return handler(line, b'')"),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--module', default=str(ROOT / 'tools/host_services.py'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        with tempfile.TemporaryDirectory() as directory:
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, name
                mutant = Path(directory) / 'host_services.py'
                mutant.write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, '-B', __file__, '--module', str(mutant)],
                                        capture_output=True, text=True)
                assert result.returncode != 0 and 'AssertionError' in result.stderr, (name, result.stderr)
                print('RED', name)
    return 0


def run(ctx):
    argv = [sys.executable, '-B', __file__]
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
