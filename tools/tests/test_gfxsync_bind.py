#!/usr/bin/env python3
"""Exercise Y4 binding and configuration fallback."""
CHECK = dict(name='gfxsync-bind-host', tier=1, needs=[], budget=5,
    covers=['gfx/sync/sync_bind.c', 'gfx/sync/sync_int.h', 'include/gfx_sync.h',
            'tools/tests/gfxsync_bind_host.c', 'tools/tests/test_gfxsync_bind.py', 'tools/tests/host32.py'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]

def observe(work, source):
    path = work / 'sync_bind.c'
    path.write_text(source)
    exe = work / 'test'
    flags = ['-I' + str(ROOT / p) for p in ('include', 'sdk/include/os32', 'gfx/sync', 'kernel/time')]
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O0',
                  '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', *flags,
                  str(path), str(ROOT / 'tools/tests/gfxsync_bind_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/sync/sync_bind.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            for anchor in ['(c->method == GFXSYNC_M_SYNC_READ && !t->sync_read)',
                           '(c->method == GFXSYNC_M_WAIT_PORT && !t->wait_port)',
                           '(c->flush == GFXSYNC_F_READBACK && !t->flush)',
                           'c->read == GFXSYNC_R_SHADOW || !t->read32']:
                assert source.count(anchor) == 1
                rc = observe(work, source.replace(anchor, '0'))
                assert rc != 0, (anchor, rc)
                print(f'RED runtime rc={rc}: {anchor}')
    print('PASS Y4 bind')

if __name__ == '__main__':
    main()

def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
