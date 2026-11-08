#!/usr/bin/env python3
"""Compare the linked DAC image with the independently recorded hardware script."""
CHECK = dict(name='trident-dac', tier=1, needs=[], budget=2.8,
    covers=['drivers/video/trident/default_dac.c', 'drivers/video/trident/trident.h',
            'userland/tests/hw/trident_mode.hwb', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py'])
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = ROOT / 'drivers/video/trident/default_dac.c'
SCRIPT = ROOT / 'userland/tests/hw/trident_mode.hwb'
DIGEST = '58d253e8f2d9b14a7dc486f5f4f04295d4c66ad04e00f3e16ce7580dfc50e6a4'
MUTATIONS = (('first', '    0, 0, 0, 0, 0, 42, 0, 42, 0, 0, 42, 42,',
              '    1, 0, 0, 0, 0, 42, 0, 42, 0, 0, 42, 42,'),
             ('middle', '    20, 20, 28, 22, 20, 28, 24, 20, 28, 26, 20, 28,',
              '    21, 20, 28, 22, 20, 28, 24, 20, 28, 26, 20, 28,'),
             ('last', '    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\n};',
              '    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,\n};'))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    pairs = re.findall(r'let save\[(\d+)\] = (\d+)', SCRIPT.read_text())
    entries = [(int(i), int(v)) for i, v in pairs if int(i) < 768]
    assert len(entries) == 768 and [i for i, _ in entries] == list(range(768))
    expected = bytes(v for _, v in entries)
    assert hashlib.sha256(expected).hexdigest() == DIGEST
    source = '#include "drivers/video/trident/trident.h"\n'
    source += 'static const u8 expected[768] = {' + ','.join(map(str, expected)) + '};\n'
    source += '''void _start(void) {
        int rc = 0;
        for (u32 i = 0; i < 768; ++i)
            if (trident_default_dac[i] != expected[i]) { rc = 1; break; }
        __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
        __builtin_unreachable();
    }\n'''
    with tempfile.TemporaryDirectory(prefix='trident-dac-') as tmp:
        work = Path(tmp)
        (work / 'test.c').write_text(source)
        original = PRODUCT.read_text()
        variants = [('original', original)]
        if args.mutate:
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, name
                variants.append((name, original.replace(old, new)))
        for name, text in variants:
            (work / 'default_dac.c').write_text(text)
            exe = work / 'test'
            subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                '-I' + str(ROOT), '-I' + str(ROOT / 'drivers/video/trident'),
                *include_flags(ROOT), str(work / 'test.c'),
                str(work / 'default_dac.c'), '-o', str(exe)], check=True)
            for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                result = host32.run([str(exe)], runner=runner, capture_output=True)
                assert result.returncode == (0 if name == 'original' else 1), (name, result)
                print(('PASS ' if name == 'original' else 'MUTATE RED ') + name + ' (' + runner + ')', flush=True)
    return 0

def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', str(Path(__file__)),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)

if __name__ == '__main__':
    raise SystemExit(main())
