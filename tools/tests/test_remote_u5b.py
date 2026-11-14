#!/usr/bin/env python3
"""Exercise service locking, child supervision and ordered shutdown."""
import argparse
import json
import os
from pathlib import Path
import signal
import importlib.util
from unittest.mock import patch
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='remote-u5b', tier=1, needs=[], budget=20,
             covers=['tools/host_agent_service.py', 'tools/host_agent.py',
                     'tools/host_agent_config.py', 'tools/host_linux.py',
                     'tools/host_notify.py', 'tools/host_operator_socket.py',
                     'tools/host_rshell.py', 'tools/host_services.py',
                     'tools/tests/test_remote_u5b.py'])

FAKE = '''import os, signal, sys, time
from pathlib import Path
log = Path(os.environ['FAKE_LOG'])
name = Path(sys.argv[0]).stem
def record(event):
    with log.open('a') as f: f.write(name + ':' + event + ':' + str(os.getpid()) + ':' + str(time.monotonic()) + ':' + repr(sys.argv[1:]) + '\\n')
def stop(sig, frame):
    record('stop')
    sys.exit(0)
signal.signal(signal.SIGTERM, stop)
record('start')
while True: time.sleep(.05)
'''


def wait_for(predicate, message):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(.05)
    raise AssertionError(message)


def test(source):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        service = root / 'host_agent_service.py'
        service.write_text(Path(source).read_text())
        (root / 'host_agent_config.py').write_text((ROOT / 'tools/host_agent_config.py').read_text())
        for name in ('host_agent', 'lan_bridge', 'lanfs_host'):
            (root / (name + '.py')).write_text(FAKE)
        config = dict(iface='lo', root=tmp, peer='02:00:00:00:00:01',
                      backend='bridge', **{'allow-write': True, 'frame-endpoint': '127.0.0.1:8026',
                      'operator-endpoint': '127.0.0.1:8027', 'state-dir': tmp + '/state'})
        config_path = root / 'config.json'
        config_path.write_text(json.dumps(config))
        log = root / 'log'
        env = dict(os.environ, FAKE_LOG=str(log))
        command = [sys.executable, '-B', str(service), '--config', str(config_path), '--lock-dir', tmp]
        proc = subprocess.Popen(command, env=env, stderr=subprocess.PIPE)
        def lines():
            return log.read_text().splitlines() if log.exists() else []
        try:
            wait_for(lambda: len(lines()) >= 3, 'children did not start')
            first = lines()
            assert all(':start:' in line for line in first)
            first = {line.split(':')[0]: line for line in first}
            assert "'--config', '" + str(config_path) + "'" in first['host_agent']
            assert "'--mac', '" + Path('/sys/class/net/lo/address').read_text().strip() + "'" in first['host_agent'], 'MAC not passed'
            assert "'--connect', '127.0.0.1:8026'" in first['lan_bridge']
            assert "'--allow-write', '/'" in first['lanfs_host'] and "'--peer', '02:00:00:00:00:01'" in first['lanfs_host']
            owner_path = config_path
            config_path = root / 'second.json'
            command[command.index('--config') + 1] = str(config_path)
            config['state-dir'] = tmp + '/other'
            config_path.write_text(json.dumps(config))
            duplicate = subprocess.Popen(command, env=env, stderr=subprocess.PIPE)
            try:
                wait_for(lambda: duplicate.poll() is not None, 'duplicate lock accepted')
                assert duplicate.returncode == 1, 'duplicate lock accepted'
                error = duplicate.communicate(timeout=8)[1].decode()
                assert 'iface lo は instance ' + owner_path.stem + ' が使っている' in error, 'lock owner missing'
            finally:
                if duplicate.poll() is None:
                    duplicate.terminate()
                duplicate.communicate(timeout=8)
            # A child failing immediately must wait at least two seconds
            # between launches, while the other children stay alive.
            pid = int(first['lan_bridge'].split(':')[2])
            os.kill(pid, signal.SIGKILL)
            wait_for(lambda: len([line for line in lines() if line.startswith('lan_bridge:start:')]) == 2,
                     'child not restarted')
            starts = [float(line.split(':')[3]) for line in lines()
                      if line.startswith('lan_bridge:start:')]
            assert starts[1] - starts[0] >= 1.9, 'restart storm'
        finally:
            proc.terminate()
            proc.communicate(timeout=20)
        assert proc.returncode == 0
        stops = [line.split(':')[0] for line in lines() if ':stop:' in line]
        assert stops == ['lanfs_host', 'lan_bridge', 'host_agent'], stops
        config['backend'] = 'np21w'
        config['allow-write'] = False
        sys.path.insert(0, str(root))
        try:
            import host_agent_service as service_module
            cmds = service_module.commands(config, str(config_path))
            assert len(cmds) == 1 and 'host_agent.py' in cmds[0][1], 'np21w raw child'
            assert '--mac' not in cmds[0]
            config['backend'] = 'bridge'
            cmds = service_module.commands(config, str(config_path))
            assert len(cmds) == 3 and '--allow-write' not in cmds[2]
            config['iface'] = 'os32-no-such-interface'
            config_path.write_text(json.dumps(config))
            before = lines()
            failed = subprocess.run(command, env=env, capture_output=True, text=True, timeout=8)
            assert failed.returncode == 1 and 'cannot read MAC' in failed.stderr, 'missing MAC accepted'
            assert lines() == before, 'unexpected child'
        finally:
            sys.path.pop(0)
    print('PASS remote-u5b')


def test_agent_config(agent_path):
    with tempfile.TemporaryDirectory() as tmp:
        config = dict(iface='test0', root=tmp, peer='02:00:00:00:00:01',
                      backend='np21w', **{'allow-write': True,
                      'frame-endpoint': '127.0.0.2:8026',
                      'operator-endpoint': '127.0.0.2:8027',
                      'state-dir': tmp + '/state'})
        path = Path(tmp) / 'config.json'
        path.write_text(json.dumps(config))
        sys.path.insert(0, str(ROOT / 'tools'))
        try:
            spec = importlib.util.spec_from_file_location('u5b_agent', agent_path)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            seen = {}
            original_agent = mod.HostAgent
            def create_agent(*args, **kwargs):
                seen['agent'] = original_agent(*args, **kwargs)
                return seen['agent']
            def operator(addr, agent, info):
                seen.update(addr=addr, info=info())
                return None
            def open_stream(args):
                seen['listen'] = args.listen
                raise OSError('fixture stop')
            with patch.object(sys, 'argv', ['host_agent', '--config', str(path), '--clip', 'none']), \
                 patch.object(mod, 'HostAgent', side_effect=create_agent), \
                 patch.object(mod, 'OperatorSocket', operator), patch.object(mod, 'open_stream', open_stream):
                assert mod.main() == 1
            assert seen['agent'].configured_peer == bytes.fromhex('020000000001'), 'configured peer not wired'
            assert seen['addr'] == ('127.0.0.2', 8027)
            assert seen['listen'] == '127.0.0.2:8026'
            assert seen['info']['config'] == dict(iface='test0', root=tmp,
                                                   allow_write=True, peer=config['peer'])
            assert not (Path(tmp) / 'state' / 'sess.txt').exists()
        finally:
            sys.path.pop(0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', default=str(ROOT / 'tools/host_agent_service.py'))
    parser.add_argument('--agent-module', default=str(ROOT / 'tools/host_agent.py'))
    args = parser.parse_args()
    test(args.module)
    test_agent_config(args.agent_module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [
            ("lock.write(Path(config_path).stem + '\\n')", "lock.write('unknown\\n')", 'lock owner missing'),
            ('RESTART_MIN = 2.0', 'RESTART_MIN = 0.0', 'restart storm'),
            ("if config['backend'] == 'bridge':", 'if True:', 'np21w raw child'),
            ('fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)', 'pass', 'duplicate lock accepted'),
            ('if child.poll() is not None:', 'if False:', 'child not restarted'),
            ("result[0] += ['--mac', mac]", 'pass', 'MAC not passed'),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'mutant.py'
            for old, new, marker in mutations:
                assert source.count(old) == 1
                path.write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, '-B', __file__, '--module', str(path)],
                                        capture_output=True, text=True)
                assert result.returncode and marker in result.stderr, result.stderr
                print('RED', marker)
            source = Path(args.agent_module).read_text()
            anchor = 'peer=parse_mac(config["peer"]) if config else None'
            assert source.count(anchor) == 1
            path.write_text(source.replace(anchor, 'peer=None'))
            result = subprocess.run([sys.executable, '-B', __file__, '--agent-module', str(path)],
                                    capture_output=True, text=True)
            assert result.returncode and 'configured peer not wired' in result.stderr, result.stderr
            print('RED configured peer not wired')


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
