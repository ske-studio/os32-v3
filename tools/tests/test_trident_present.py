#!/usr/bin/env python3
"""Trident CPU present: independently checked image, bounds, trace and failures."""
CHECK = dict(name='trident-present', tier=1, needs=[], budget=3,
    covers=['drivers/video/trident/present.c', 'drivers/video/trident/trident.h',
            'tools/tests/trident_present_host.c', 'tools/tests/trident_device_fake.c',
            'tools/tests/trident_device_fake.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'include/types.h', 'include/memmap.h',
            'include/pegc.h', 'gfx/palette.h'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'drivers/video/trident/present.c'
MUTANTS = [('M_LAST_ROW', 'row < (u32)h;', 'row + 1u < (u32)h;'),
           ('M_SOURCE_END', 'end > r->client_bytes', 'end > TRI_DISPLAY_BYTES'),
           ('M_DROP_RC', 'if (rc) return rc;\n            off += count', 'if (rc) return TRI_OK;\n            off += count')]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = SOURCE.read_text()
    with tempfile.TemporaryDirectory(prefix='trident-present-') as tmp:
        work = Path(tmp)
        for name, old, new in [('normal', None, None)] + (MUTANTS if args.mutate else []):
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
                *include_flags(ROOT), str(path), str(ROOT / 'tools/tests/trident_device_fake.c'),
                str(ROOT / 'tools/tests/trident_present_host.c'), '-o', str(exe)], check=True)
            for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                result = host32.run([str(exe)], runner=runner, capture_output=True)
                assert result.returncode == (0 if old is None else 1), (name, result)
                print(('PASS ' if old is None else 'MUTATE RED ') + name + ' ' + runner, flush=True)
    return 0

def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_present.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)

if __name__ == '__main__':
    raise SystemExit(main())
