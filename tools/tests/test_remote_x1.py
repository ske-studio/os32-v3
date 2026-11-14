#!/usr/bin/env python3
"""Verify registry IDs stay below the LAN operator reservations."""
import argparse
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='remote-x1', tier=1, needs=[], budget=5,
             covers=['tools/host_services.py', 'include/remote_control.h',
                     'tools/tests/test_remote_x1.py'])


def test(path):
    spec = importlib.util.spec_from_file_location('host_services_x1', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    header = (ROOT / 'include/remote_control.h').read_text()
    def value(name):
        return int(re.search(r'^#define\s+' + name + r'\s+(\d+)u', header, re.M).group(1))
    limit = value('REMOTE_SVC_REGISTRY_MAX')
    reserved = {value('REMOTE_SVC_' + name) for name in ('RSHELL', 'GSERIAL', 'PPC_DIAG')}
    assert module.MAX_SERVICE_ID == limit
    registry = module.Registry(1)
    for i in range(limit - len(registry._services)):
        last = registry.register('test.%d' % i, 1, 'plugin', None)
    assert last == limit
    ids = {int(line.split()[2]) for line in registry.list_bytes().decode().splitlines()[1:]}
    assert len(ids) == limit and not ids.intersection(reserved)
    try:
        registry.register('overflow', 1, 'plugin', None)
    except RuntimeError:
        pass
    else:
        raise AssertionError('reserved ID allocated')
    for number in reserved:
        registry._services['legacy'] = (1, number, 'plugin', None)
        for action in (lambda: registry.register('legacy', 1, 'plugin', None),
                       registry.list_bytes):
            try:
                action()
            except RuntimeError:
                pass
            else:
                raise AssertionError('reserved existing ID accepted')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--module', default=str(ROOT / 'tools/host_services.py'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        old, new = 'MAX_SERVICE_ID = 65531 ', 'MAX_SERVICE_ID = 65534 '
        assert source.count(old) == 1
        with tempfile.TemporaryDirectory() as directory:
            mutant = Path(directory) / 'host_services.py'
            mutant.write_text(source.replace(old, new))
            result = subprocess.run([sys.executable, '-B', __file__, '--module', str(mutant)],
                                    capture_output=True, text=True)
            assert result.returncode != 0 and 'AssertionError' in result.stderr
            print('RED registry-limit rc=%d' % result.returncode)
    print('PASS remote-x1')


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
