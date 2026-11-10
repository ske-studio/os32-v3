#!/usr/bin/env python3
"""Exercise the product clock gate against a fake native MMIO bus."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path

CHECK = dict(name='ppc-lan-p13', tier=2, budget=60,
             covers=['platform/powermac/gmac_clock.c', 'tools/ppc/test_lan_p13.py',
                     'tools/ppc/lan_p13_fake.c'],
             inputs=['platform/powermac/lan_boot.h', 'include/types.h'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('rmw', 'value | 2u', '(value & 0u) | 2u', 0, 'H:rmw'),
    ('readback', 'if (!(pm_clock_read(clock_va) & 2u)) return LAN_EIO;',
     '(void)pm_clock_read(clock_va);', 1, 'H:readback'),
    ('profile', 'b->profile != PM_GMAC_PROFILE_UNINORTH', '0', 2, 'H:profile'),
    ('bit', 'value | 2u', 'value | 1u', 0, 'H:rmw'),
]


def check(source, temp, case):
    exe = temp / 'test'
    cmd = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
           '-nostdlib', '-static', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror',
           '-DPM_GMAC_CLOCK_FAKE', '-DCASE=' + str(case), '-I.', '-Iinclude',
           '-Iplatform/powermac', str(source), 'tools/ppc/lan_p13_fake.c', '-o', str(exe)]
    built = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    if built.returncode:
        raise RuntimeError('compile: ' + built.stderr)
    return subprocess.run(['qemu-i386', str(exe)], cwd=ROOT, text=True, capture_output=True, timeout=30)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'platform/powermac/gmac_clock.c').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as directory:
        temp = Path(directory)
        source = temp / 'gmac_clock.c'
        source.write_text(original)
        for case in range(4):
            result = check(source, temp, case)
            if result.returncode or 'REACHED host' not in result.stdout.splitlines():
                raise RuntimeError('baseline %d: %s' % (case, result.stdout + result.stderr))
        if args.mutate:
            for name, old, new, case, diagnostic in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError('nonunique anchor: ' + name)
                source.write_text(original.replace(old, new, 1))
                result = check(source, temp, case)
                if result.returncode != 1 or diagnostic not in result.stdout.splitlines() or 'REACHED host' not in result.stdout.splitlines():
                    raise RuntimeError('mutation %s: %s' % (name, result.stdout + result.stderr))
                print('RED', name, diagnostic)
    print('PASS ppc-lan-p13')


if __name__ == '__main__':
    main()
