#!/usr/bin/env python3
"""Probe against the shared device fake; script expectations H:70-81."""
CHECK = dict(name='trident-probe', tier=1, needs=[], budget=3,
    covers=['drivers/video/trident/probe.c', 'drivers/video/trident/trident.h',
            'tools/tests/trident_probe_host.c', 'tools/tests/trident_device_fake.c',
            'tools/tests/trident_device_fake.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = ROOT / 'drivers/video/trident/probe.c'
MUTATIONS = [
    ('chip', 'chip != TRI_CHIP_9682', 'chip == TRI_CHIP_9682'),
    ('arm', '!d->pinned || !d->armed || !d->res->ready', 'false'),
    ('bar', '(pci->bar1 & 0x07u)', 'false'),
]
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = PRODUCT.read_text()
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'probe.c'
        exe = Path(tmp) / 'probe'
        def run(text):
            src.write_text(text)
            subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                '-I' + str(ROOT), '-I' + str(ROOT / 'drivers/video/trident'),
                *include_flags(ROOT), str(src), str(ROOT / 'tools/tests/trident_probe_host.c'),
                str(ROOT / 'tools/tests/trident_device_fake.c'), '-o', str(exe)], check=True)
            return host32.run([str(exe)]).returncode
        assert run(original) == 0
        print('GREEN', flush=True)
        if args.mutate:
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, name
                rc = run(original.replace(old, new))
                assert rc > 0, (name, rc)
                print(f'MUTATE RED {name}: rc={rc}', flush=True)
    return 0

def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)

if __name__ == '__main__':
    raise SystemExit(main())
