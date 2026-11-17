#!/usr/bin/env python3
"""Exercise the real WM polling and notice delivery on the host."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cmd = [sys.executable, '-B', 'userland/gshell/host/integration.py']
    if args.mutate:
        cmd.append('--mutate')
    return subprocess.call(cmd, cwd=ROOT, env=dict(os.environ, MUTATION_FILTER='remote-g3'))


def run(ctx):
    cmd = [sys.executable, '-B', 'tools/tests/test_remote_g3.py']
    if ctx.mutate:
        cmd.append('--mutate')
    return subprocess.call(cmd, cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
