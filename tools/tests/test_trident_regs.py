#!/usr/bin/env python3
"""Trident 3b register widths/sequences and real bind/callback assembly.

Trace fixtures live in trident_regs_host.c; no external reference checkout is
needed in CI. Privileged outp alone is replaced for the production host run.
"""
CHECK = dict(name='trident-regs', tier=1, needs=[], budget=4.8,
    covers=['drivers/video/trident/regs.c', 'drivers/video/trident/trident.h',
            'tools/tests/trident_regs_host.c', 'tools/tests/trident_device_fake.c',
            'tools/tests/trident_device_fake.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk',
            'include/types.h', 'include/memmap.h', 'gfx/palette.h', 'include/io.h',
            'arch/x86/arch_io.h', 'platform/pc98/platform_io.h'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'drivers/video/trident/regs.c'
HOST = 'tools/tests/trident_regs_host.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
# Unique product anchors. Each must compile and be rejected by the executable.
MUTATIONS = (
    ('mmio-read-width', '*v = *(volatile u##bits *)(base(r, s) + off);',
     '*v = *(volatile u8 *)(base(r, s) + off);'),
    ('mmio-write-width', '*(volatile u##bits *)(base(r, s) + off) = v;',
     '*(volatile u8 *)(base(r, s) + off) = v;'),
    ('range-last-byte', 'width > limit - off', 'width >= limit - off'),
    ('span-last-word', 'i < count; ++i) dst[i] = src[i];',
     'i + 1 < count; ++i) dst[i] = src[i];'),
    ('span-whole-range', 'if (count > (limit - off) / 4)',
     'if (count > (limit - off) / 4 + 1)'),
    ('bind-span', '.port8 = port8, .write_span32 = write_span32',
     '.port8 = port8, .write_span32 = 0 ? write_span32 : 0'),
    ('indexed-word', '(u16)(index | ((u16)v << 8))', '(u16)(index | ((u16)v << 0))'),
    ('ar-flip', 'TRY(READ(vga_base + 6u, &discard));\n    TRY(WRITE(TRI_AR_INDEX, index));\n    return WRITE(TRI_AR_INDEX, v);',
     'TRY(READ(vga_base + 5u, &discard));\n    TRY(WRITE(TRI_AR_INDEX, index));\n    return WRITE(TRI_AR_INDEX, v);'),
    ('hidden-four-reads', 'i < 4; ++i) TRY(READ(TRI_DAC_MASK, &discard));',
     'i < 3; ++i) TRY(READ(TRI_DAC_MASK, &discard));'),
    ('hidden-reset', 'TRY(READ(TRI_DAC_WRITE_INDEX, &discard));\n    *v = value;',
     'TRY(READ(TRI_DAC_MASK, &discard));\n    *v = value;'),
    ('ext-reselect', 'TRY(WRITE(TRI_EXT_DAC_INDEX, 4));\n    return WRITE',
     'TRY(WRITE(TRI_EXT_DAC_INDEX, 5));\n    return WRITE'),
    ('protect-display', 'seq & 0xDF', 'seq & 0xFF'),
    ('protect-reset', 'vga_write(d, TRI_SR_INDEX, 0, 1)',
     'vga_write(d, TRI_SR_INDEX, 0, 3)'),
    ('vga-byte-writes', 'TRY(WRITE(port, index));\n    return WRITE(port + 1u, v);',
     'return trident_indexed_write(d, port, index, v);'),
    ('crtc-lock', 'lock ? (v | 0x80) : (v & 0x7F)',
     'lock ? (v | 0x00) : (v & 0x7F)'),
    ('propagate-rc', 'if (rc_) return rc_;', 'if (rc_) return 0;'),
)


def build(work, tree):
    exe = work / 'regs-host'
    subprocess.run(['gcc', *FLAGS, '-I' + str(tree), '-I' + str(ROOT),
                    '-I' + str(ROOT / 'drivers/video/trident'),
                    *include_flags(ROOT), '-nostdlib', '-static', '-no-pie',
                    str(ROOT / HOST), str(ROOT / 'tools/tests/trident_device_fake.c'),
                    '-o', str(exe)], check=True, cwd=ROOT)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='trident-regs-') as tmp:
        work = Path(tmp)
        # Also compile the untouched platform include path with the real outp.
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        for compiler in ('gcc', str(cross)):
            subprocess.run([compiler, *FLAGS, '-I' + str(ROOT), *include_flags(ROOT),
                            '-c', str(ROOT / PRODUCT), '-o', str(work / 'regs.o')], check=True)
            print(f'PASS production TU ({compiler})', flush=True)
        runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
        assert runners
        exe = build(work, ROOT)
        for runner in runners:
            result = host32.run([str(exe)], runner=runner, capture_output=True)
            assert result.returncode == 0 and result.stdout == b'GREEN\n', result
            print(f'PASS sequences/failures/production assembly ({runner})', flush=True)
        if args.mutate:
            tree = work / 'tree'
            target = tree / PRODUCT
            target.parent.mkdir(parents=True)
            original = (ROOT / PRODUCT).read_text()
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, original.count(old))
                target.write_text(original.replace(old, new))
                exe = build(work, tree)  # compile failure is never runtime RED
                for runner in runners:
                    result = host32.run([str(exe)], runner=runner, capture_output=True)
                    assert result.returncode == 1 and result.stdout.startswith(b'RED line '), (name, result)
                    print(f'MUTATE RED {name} ({runner}): {result.stdout.decode().strip()}', flush=True)
    return 0


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_regs.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    raise SystemExit(main())
