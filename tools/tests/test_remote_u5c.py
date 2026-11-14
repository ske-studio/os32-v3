#!/usr/bin/env python3
"""Check the installed service contract and its mutation sensitivity."""
import argparse
import configparser
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
UNIT = ROOT / 'tools/systemd/os32-host-agent@.service'
README = ROOT / 'tools/systemd/README.md'
CHECK = dict(name='remote-u5c', tier=1, needs=[], budget=5,
             covers=['tools/systemd/os32-host-agent@.service',
                     'tools/systemd/README.md', 'tools/tests/test_remote_u5c.py'])


def test(path, readme):
    instructions = Path(readme).read_text()
    assert '-m 0644 tools/systemd/host-agent.example.json' in instructions, 'config mode changed'
    assert '-m 0755 /etc/os32/host-agent' in instructions, 'config directory mode changed'
    unit = configparser.ConfigParser(interpolation=None, strict=True)
    unit.read_string(Path(path).read_text())
    service = unit['Service']
    assert service.get('ExecStart') == ('/usr/bin/python3 /opt/os32/tools/'
        'host_agent_service.py --config /etc/os32/host-agent/%i.json'), 'ExecStart changed'
    assert service.get('Type') == 'simple', 'Type changed'
    assert service.get('Restart') == 'on-failure', 'Restart missing or changed'
    assert service.get('RestartSec') == '2', 'RestartSec changed'
    assert service.get('User') == 'os32-host', 'User changed'
    for field in ('AmbientCapabilities', 'CapabilityBoundingSet'):
        assert service.get(field) == 'CAP_NET_RAW', field + ' missing CAP_NET_RAW'
    assert unit['Install'].get('WantedBy') == 'multi-user.target', 'WantedBy changed'
    if shutil.which('systemd-analyze'):
        result = subprocess.run(['systemd-analyze', 'verify', str(path)],
                                capture_output=True, text=True)
        if result.returncode and 'Failed to enable SO_PASSCRED on handoff timestamp socket: Operation not permitted' in result.stderr:
            print('SKIP systemd-analyze verify: sandbox denies SO_PASSCRED')
        else:
            assert result.returncode == 0, result.stderr
    print('PASS remote-u5c')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--unit', default=str(UNIT))
    parser.add_argument('--readme', default=str(README))
    args = parser.parse_args()
    test(args.unit, args.readme)
    if args.mutate:
        source = Path(args.unit).read_text()
        mutations = [('Restart=on-failure\n', '', 'Restart missing or changed'),
                     ('AmbientCapabilities=CAP_NET_RAW\n', '',
                      'AmbientCapabilities missing CAP_NET_RAW')]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'os32-host-agent@.service'
            for old, new, marker in mutations:
                assert source.count(old) == 1
                path.write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, '-B', __file__, '--unit', str(path)],
                                        capture_output=True, text=True)
                assert result.returncode and marker in result.stderr, result.stderr
                print('RED', marker)
            source = Path(args.readme).read_text()
            old = '-m 0644 tools/systemd/host-agent.example.json'
            assert source.count(old) == 1
            path = Path(tmp) / 'mutant.txt'
            path.write_text(source.replace(old, old.replace('0644', '0640')))
            result = subprocess.run([sys.executable, '-B', __file__, '--readme', str(path)],
                                    capture_output=True, text=True)
            assert result.returncode and 'config mode changed' in result.stderr, result.stderr
            print('RED config mode changed')


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
