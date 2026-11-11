#!/usr/bin/env python3
"""Exercise sync row transfers without libc."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

CHECK = dict(name='gfxsync-write-host', tier=1, needs=[], budget=15,
             covers=['gfx/sync/sync_write.c', 'gfx/sync/sync_int.h', 'include/gfx_sync.h',
                     'tools/tests/gfxsync_write_host.c', 'tools/tests/test_gfxsync_write.py',
                     'tools/tests/host32.py', 'tools/tests/kernel_includes.py'])
ROOT = Path(__file__).resolve().parents[2]

def run_one(work, source):
    exe = work / 'test'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    '-nostdlib', '-static', '-no-pie', *include_flags(ROOT), '-I' + str(ROOT / 'gfx/sync'),
                    str(source), str(ROOT / 'tools/tests/gfxsync_write_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)], runner='qemu', timeout=15).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        source = ROOT / 'gfx/sync/sync_write.c'
        assert run_one(work, source) == 0
        if args.mutate:
            body = source.read_text()
            old = 'h - 1 > (c->t.bytes - row - n) / c->t.pitch'
            assert body.count(old) == 1
            mutant = work / 'sync_write.c'
            mutant.write_text(body.replace(old, '0'))
            assert run_one(work, mutant) != 0
            print('RED row bounds mutation')
    print('PASS sync row transfers')

def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)

if __name__ == '__main__':
    main()
