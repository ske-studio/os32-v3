#!/usr/bin/env python3
"""Trident RAM transfer policies and direct VRAM synchronization."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'drivers/video/trident/present.c'
MUTANTS = [
    ('target-offset', '.va = d->res->lfb + v->off', '.va = d->res->lfb'),
    ('publish-head', 'gfx2_sync_publish(GFX2_KIND_TRIDENT, 0, d->sync_ctx)', 'gfx2_sync_publish(GFX2_KIND_TRIDENT, 1, d->sync_ctx)'),
    ('unbind-order', '    gfx2_sync_publish(GFX2_KIND_TRIDENT, 0, NULL);', '    (void)0;'),
    ('generation', 'd->sync_generation == d->res->generation', 'd->sync_generation != 0'),
    ('override-method', 'methods[method],', '(methods[method] & 0),'),
    ('override-clear', '    gfxsync_override_clear(d->sync_ctx);', '    (void)d;'),
    ('row-error', 'return gfxsync_write_rows(d->sync_ctx,', 'return !gfxsync_write_rows(d->sync_ctx,'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = SOURCE.read_text()
    with tempfile.TemporaryDirectory(prefix='trident-present-') as tmp:
        work = Path(tmp)
        for name, old, new in ([('normal', None, None)] +
                               (MUTANTS + [('restored', None, None)] if args.mutate else [])):
            source = original
            if old is not None:
                assert source.count(old) == 1
                source = source.replace(old, new, 1)
            path = work / 'present.c'
            path.write_text(source)
            exe = work / 'host'
            subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                '-I' + str(ROOT), '-I' + str(ROOT / 'drivers/video/trident'),
                *include_flags(ROOT), str(path), str(ROOT / 'drivers/video/trident/regs.c'), str(ROOT / 'tools/tests/trident_device_fake.c'), str(ROOT / 'tools/tests/trident_sync_fake.c'),
                str(ROOT / 'tools/tests/trident_sync_host.c'), '-o', str(exe)], check=True)
            for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                result = host32.run([str(exe)], runner=runner, capture_output=True)
                assert result.returncode == (0 if old is None else 1), (name, result)
                print(('PASS ' if old is None else 'MUTATE RED ') + name + ' ' + runner, flush=True)
    return 0

def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_sync.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)

if __name__ == '__main__':
    raise SystemExit(main())
