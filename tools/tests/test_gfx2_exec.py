#!/usr/bin/env python3
"""Exercise the actual exec teardown with fake release and ledger edges."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'exec/exec_finish.inc'
ANCHOR = '    if (gfx2_owner_release(a->as))\n        paging_addrspace_poison(a->as);'

def run_case(tmp, mutate, failure):
    src = SOURCE.read_text()
    assert src.count(ANCHOR) == 1
    if mutate:
        src = src.replace(ANCHOR, '    (void)gfx2_owner_release(a->as);')
    start = src.index('static void exec_teardown_app(')
    end = src.index('\n}\n', start) + 2
    (tmp / 'teardown.inc').write_text(src[start:end])
    fixture = (ROOT / 'tools/tests/gfx2_exec_host.c').read_text()
    if failure:
        fixture = fixture.replace('static int release_rc, order,', 'static int release_rc = -1;\nstatic int order,')
    (tmp / 'fixture.c').write_text(fixture)
    cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie', str(tmp/'fixture.c'), '-o', str(tmp/'test')]
    result = host32.build(cmd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return host32.run([str(tmp/'test')], runner='qemu', capture_output=True, text=True).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as d:
        tmp = Path(d)
        for failure in (False, True):
            rc = run_case(tmp, False, failure)
            print('GREEN release failure=%s rc=%d' % (failure, rc), flush=True)
            if rc: return 1
        if args.mutate:
            rc = run_case(tmp, True, True)
            print('RED remove poison rc=%d' % rc, flush=True)
            if rc != 2: return 1
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
