#!/usr/bin/env python3
"""Configuration validation and NIC instance isolation."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='remote-u5a', tier=1, needs=[], budget=3,
             covers=['tools/host_agent_config.py',
                     'tools/systemd/host-agent.example.json',
                     'tools/tests/test_remote_u5a.py'])


def test(path):
    spec = importlib.util.spec_from_file_location('remote_u5a', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    example = json.loads((ROOT / 'tools/systemd/host-agent.example.json').read_text())
    config = mod.validate_config(example)
    assert mod.config_summary(config) == dict(iface='eth0', root='/srv/os32',
                                              allow_write=False, peer='02:00:00:00:00:01')
    omitted = dict(example)
    del omitted['allow-write']
    assert mod.validate_config(omitted)['allow-write'] is False, 'write default changed'
    def rejected(value, marker):
        try:
            mod.validate_config(value)
        except ValueError:
            return
        raise AssertionError(marker)
    bad = dict(example, root='relative')
    rejected(bad, 'relative root accepted')
    for key, value in [('iface', ''), ('root', ''), ('allow-write', 1),
                       ('peer', '00:11:22:33:44:zz'), ('backend', 'tap'),
                       ('frame-endpoint', 'host:no'), ('operator-endpoint', ':8027'),
                       ('state-dir', ''), ('unexpected', 'x')]:
        bad = dict(example)
        bad[key] = value
        rejected(bad, key + ' accepted')
    for key in set(example) - {'allow-write'}:
        bad = dict(example)
        del bad[key]
        rejected(bad, 'missing ' + key)
    other = dict(example, iface='eth1', **{'frame-endpoint': '127.0.0.2:8026',
                 'operator-endpoint': '127.0.0.2:8027', 'state-dir': '/var/lib/os32-host-agent/eth1'})
    mod.validate_instances([example, other])
    for key in ('iface', 'frame-endpoint', 'operator-endpoint', 'state-dir'):
        bad = dict(other)
        bad[key] = example[key]
        try:
            mod.validate_instances([example, bad])
        except ValueError:
            continue
        raise AssertionError('collision accepted: ' + key)
    for address in ('0.0.0.0:8026', '127.0.0.1:8027'):
        bad = dict(other, **{'frame-endpoint': address})
        try:
            mod.validate_instances([example, bad])
        except ValueError:
            continue
        raise AssertionError('overlapping endpoint accepted')
    with tempfile.TemporaryDirectory() as directory:
        file = Path(directory) / 'config.json'
        file.write_text(json.dumps(omitted))
        assert mod.load_config(file)['allow-write'] is False
        assert list(Path(directory).iterdir()) == [file]
    print('PASS remote-u5a')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/host_agent_config.py'))
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [
            ("config.get('allow-write', False)", "config.get('allow-write', True)", 'write default changed'),
            ("if not os.path.isabs(config['root']):", 'if False:', 'relative root accepted'),
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'host_agent_config.py'
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
