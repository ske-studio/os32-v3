#!/usr/bin/env python3
"""Trident RAM byte present and direct VRAM synchronization."""
CHECK = dict(name='trident-present', tier=1, needs=[], budget=3,
    covers=['drivers/video/trident/present.c', 'drivers/video/trident/trident.h',
            'tools/tests/trident_present_host.c', 'tools/tests/trident_device_fake.c',
            'tools/tests/trident_device_fake.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'drivers/video/trident/regs.c',
            'gfx/gfx_internal.h', 'gfx/gfx.h', 'include/gfx_hal.h', 'include/gfx_format.h',
            'include/gfx_mode.h', 'mm/kmalloc.h', 'sdk/include/os32/os32_kapi_shared.h',
            'sdk/include/os32/os32_generations.h', 'include/types.h', 'include/memmap.h',
            'include/pegc.h', 'gfx/palette.h', 'build/config.mk'])
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
    ('skip-sync', 'int rc = tri_sync(d);', 'int rc = TRI_OK;'),
    ('drop-rc', 'if (rc) return rc;\n    if (!d->res', 'if (rc) return TRI_OK;\n    if (!d->res'),
    ('ram-no-copy', 'if (!d->res->ram_fallback) return TRI_OK;', 'return TRI_OK;'),
    ('direct-copy', 'if (!d->res->ram_fallback) return TRI_OK;', 'if (false) return TRI_OK;'),
    ('store32', 'trident_write8(d, TRI_SPACE_LFB, dst + i, d->res->client[src + i])',
     'trident_write32(d, TRI_SPACE_LFB, dst + i, d->res->client[src + i])'),
    ('skip-dword-read', 'rc = trident_read8(d, TRI_SPACE_REGS, TRI_CR_COLOR + 6u, &status);', 'rc = TRI_OK; (void)status;'),
    ('wrong-dword', '((u32)p[3] << 24)', '((u32)p[2] << 24)'),
    ('fixed-pitch', 'row * v->pitch', 'row * TRI_PITCH'),
    ('drop-cpp', 'u32 bytes = (u32)w * c->cpp;', 'u32 bytes = (u32)w;'),
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
                *include_flags(ROOT), str(path), str(ROOT / 'drivers/video/trident/regs.c'), str(ROOT / 'tools/tests/trident_device_fake.c'),
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
