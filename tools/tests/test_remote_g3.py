#!/usr/bin/env python3
"""Exercise the real WM polling and notice delivery on the host."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

CHECK = dict(name='remote-g3', tier=1, needs=[], budget=20, covers=[
    'userland/gshell/**', 'sdk/rust/os32api/**', 'userland/lib/font/**',
    'tools/tests/os32api_host.py', 'tools/tests/g16_fixture.py',
    'tools/fetch_fonts.py', 'tools/gen_g16.py', 'tools/g16/**',
    'assets/fonts/**', 'assets/ipadic/*.csv', 'include/remote_control.h',
    'include/callconv.h', 'include/abi.h', 'arch/x86/arch_abi.h',
    'net/link.h', 'exec/shell_service.h',
    'sdk/include/os32/**', 'tools/tests/test_remote_g3.py'])
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
