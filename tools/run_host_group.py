#!/usr/bin/env python3
"""Run unchanged host checks together, sharing the parent make jobserver.

Each script is a separate make job. A host32 script still runs its normal
control on every requested runner and mutations on the first runner only.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


def commands(script, host32, mutate, runners):
    base = [sys.executable, '-B', script]
    if not host32:
        return [base + (['--mutate'] if mutate else [])]
    if not runners:
        raise ValueError('HOST32_RUNNERS must not be empty')
    return [base + ['--runner', runner] + (['--mutate'] if mutate and i == 0 else [])
            for i, runner in enumerate(runners)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host32', action='append', default=[])
    parser.add_argument('--plain', action='append', default=[])
    parser.add_argument('--control', action='append', default=[])
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    scripts = ([(p, True, args.mutate) for p in args.host32] +
               [(p, False, args.mutate) for p in args.plain] +
               [(p, False, False) for p in args.control])
    if not scripts:
        parser.error('at least one script is required')
    runners = os.environ.get('HOST32_RUNNERS', 'native qemu').split()
    lines = ['.PHONY: all', 'all: ' + ' '.join(f'part-{i}' for i in range(len(scripts)))]
    for i, (script, host32, mutate) in enumerate(scripts):
        lines += [f'.PHONY: part-{i}', f'part-{i}:']
        for command in commands(script, host32, mutate, runners):
            # make expands dollars before the shell parses its quoted arguments.
            lines.append('\t' + shlex.join(command).replace('$', '$$'))
    # GNU make's FIFO jobserver is inherited by name. Older pipe jobservers
    # need their descriptors forwarded through Python as well (recipe uses +).
    auth = re.search(r'--jobserver-(?:auth|fds)=(\d+),(\d+)', os.environ.get('MAKEFLAGS', ''))
    fds = tuple(map(int, auth.groups())) if auth else ()
    with tempfile.TemporaryDirectory(prefix='os32-host-group-') as directory:
        makefile = Path(directory) / 'checks.mk'
        makefile.write_text('\n'.join(lines) + '\n')
        return subprocess.call(['make', '--no-print-directory', '--output-sync=target',
                                '-f', str(makefile), 'all'], pass_fds=fds)


if __name__ == '__main__':
    raise SystemExit(main())
