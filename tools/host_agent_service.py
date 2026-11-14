#!/usr/bin/env python3
"""Run and supervise the host services for one configured NIC."""
import argparse
import fcntl
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

from host_agent_config import load_config

TOOLS = Path(__file__).resolve().parent
RESTART_MIN = 2.0


def commands(config, config_path):
    result = [[sys.executable, str(TOOLS / 'host_agent.py'), '--config', config_path]]
    if config['backend'] == 'bridge':
        result.append([sys.executable, str(TOOLS / 'lan_bridge.py'), '--iface',
                       config['iface'], '--connect', config['frame-endpoint']])
        lanfs = [sys.executable, str(TOOLS / 'lanfs_host.py'), '--iface', config['iface'],
                 '--root', config['root'], '--peer', config['peer']]
        if config['allow-write']:
            lanfs += ['--allow-write', '/']
        result.append(lanfs)
    return result


def run(config, config_path, lock_dir):
    iface = config['iface']
    if '/' in iface or iface in ('.', '..'):
        raise ValueError('invalid iface for lock filename')
    lock_path = Path(lock_dir) / ('os32-host-agent-' + iface + '.lock')
    with lock_path.open('a+') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            lock.seek(0)
            owner = lock.read().strip() or 'unknown'
            raise ValueError(f'iface {iface} は instance {owner} が使っている') from None
        lock.seek(0)
        lock.truncate()
        lock.write(Path(config_path).stem + '\n')
        lock.flush()
        stopping = False
        def stop(_signum, _frame):
            nonlocal stopping
            stopping = True
        previous = {sig: signal.signal(sig, stop) for sig in (signal.SIGTERM, signal.SIGINT)}
        children = []
        try:
            for command in commands(config, config_path):
                if stopping:
                    break
                children.append((command, subprocess.Popen(command), time.monotonic()))
            while not stopping:
                for index, (command, child, started) in enumerate(children):
                    if child.poll() is not None:
                        if time.monotonic() - started >= RESTART_MIN:
                            children[index] = (command, subprocess.Popen(command), time.monotonic())
                time.sleep(0.2)
        finally:
            for _, child, _ in reversed(children):
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()
            for sig, handler in previous.items():
                signal.signal(sig, handler)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True)
    parser.add_argument('--lock-dir', default='/run/lock')
    args = parser.parse_args()
    try:
        config = load_config(args.config)
        return run(config, os.path.abspath(args.config), args.lock_dir)
    except (OSError, ValueError) as exc:
        print('host-agent-service: ' + str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
