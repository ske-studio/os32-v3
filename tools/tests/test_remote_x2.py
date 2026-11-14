#!/usr/bin/env python3
"""Exercise reserved mailbox filtering and its removal mutation."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
import test_host_mailbox

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='remote-x2', tier=1, needs=[], budget=5, covers=[
    'net/host_mailbox.c', 'net/host_mailbox.h', 'include/remote_control.h',
    'net/link.h', 'sdk/include/os32/os32_host_notice.h',
    'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/host_mailbox_host.c',
    'tools/tests/hostsvc_fixture.c', 'tools/tests/hostsvc_fixture.h',
    'tools/tests/test_host_mailbox.py', 'tools/tests/test_remote_x2.py',
    'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk',
    'include/io.h', 'lib/kstring.h', 'mm/kmalloc.h'])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='remote-x2-') as tmp:
        work = Path(tmp)
        source = ROOT / 'net/host_mailbox.c'
        def run(path):
            exe = test_host_mailbox.build(work, path)
            return host32.run([str(exe), 'reserved', 'test'],
                              capture_output=True, timeout=30).returncode
        assert run(source) == 0, 'normal control failed'
        print('PASS remote-x2 (runner=' + host32.selected_runner() + ')', flush=True)
        if args.mutate:
            original = source.read_text()
            old = '''return notice_service != REMOTE_SVC_RSHELL &&
               notice_service != REMOTE_SVC_GSERIAL &&
               notice_service != REMOTE_SVC_PPC_DIAG;'''
            assert original.count(old) == 1, 'mutation anchor must be unique'
            mutant = work / 'host_mailbox.c'
            mutant.write_text(original.replace(old, 'return 1;'))
            rc = run(mutant)
            assert rc == 1, ('reserved-filter', rc)
            print('RED reserved-filter rc=1', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        env = dict(os.environ, HOST32_RUNNERS=runner)
        cmd = [sys.executable, '-B', 'tools/tests/test_remote_x2.py']
        if ctx.mutate and index == 0:
            cmd.append('--mutate')
        rc = subprocess.call(cmd, cwd=ctx.root, env=env)
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
