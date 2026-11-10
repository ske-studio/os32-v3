#!/usr/bin/env python3
"""Exercise the product OF capture against an ILP32 firmware fixture."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32

CHECK = dict(name='ppc-lan-p3', tier=2, budget=60,
             covers=['platform/powermac/gmac_tree.c', 'tools/ppc/lan_p3_fake.c',
                     'tools/ppc/test_lan_p3.py'],
             inputs=['platform/powermac/lan_boot.h', 'platform/powermac/ofw.h',
                     'arch/ppc/ppc_boot.h', 'include/types.h', 'lib/kstring.h',
                     'lib/kstring_c.c', 'tools/tests/host32.py'])


MUTATIONS = [
    ('bar', '(hi&255)==0x10', '(hi&255)==0x14', 0, 'capture'),
    ('unknown-id', 'vendor!=0x106b || device!=0x21', '0', 13, 'capture'),
    ('bar-order', '(hi&255)==0x10', 'i==0', 14, 'value'),
    ('window', 'b.bar_bus-w->pci_base<=w->bytes &&\n            b.bar_bytes<=w->bytes-(b.bar_bus-w->pci_base)', '1', 5, 'capture'),
    ('unique', 'found!=1', 'found>2', 2, 'capture'),
    ('multicast', '(b.mac[0]&1)', '0', 10, 'capture'),
    ('bridge', '!compat(bridge,"uni-north")', '0', 3, 'capture'),
    ('parent', 'parent!=root', '0', 15, 'capture'),
    ('bridge-size-high', 'sc==2 && be32(prop+ac*4)', '0', 16, 'capture'),
    ('bridge-overflow', 'be32(prop+(ac+sc-1)*4)>0xffffffffu-b.root_pa', '0', 17, 'capture'),
    ('size-high', 'be32(p+(3+ac)*4)', '0', 18, 'capture'),
    ('pci-overflow', 'w->bytes>0xffffffffu-w->pci_base', '0', 19, 'capture'),
    ('cpu-overflow', 'w->bytes>0xffffffffu-w->cpu_base', '0', 20, 'capture'),
    ('cpu-high', 'ac==2 && be32(p+12)', '0', 21, 'capture'),
    ('peer', 'b->peer_mac[j]=val[j]', 'b->peer_mac[j]=0xff', 11, 'peer'),
]


def exercise(source, directory, case=None):
    exe = directory / 'capture'
    subprocess.run(['gcc', '-std=gnu11', '-m32', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie',
                    '-fno-stack-protector', '-nostdlib', '-static', '-D__cdecl=',
                    *['-I'+str(ROOT / p) for p in ('platform/powermac', 'arch/ppc', 'include', 'lib')],
                    str(source), str(ROOT / 'tools/ppc/lan_p3_fake.c'),
                    str(ROOT / 'lib/kstring_c.c'), '-o', str(exe)], check=True, timeout=60)
    failures = []
    for index in (range(26) if case is None else [case]):
        result = host32.run([str(exe), str(index)], capture_output=True, timeout=10)
        if result.returncode:
            failures.append((index, result.returncode, result.stdout))
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='lan-p3-') as temp:
        failures = exercise(ROOT / 'platform/powermac/gmac_tree.c', Path(temp))
        if failures:
            raise RuntimeError(f'baseline failures: {failures}')
        print('GREEN: 26 firmware-tree cases')
        if args.mutate:
            product = (ROOT / 'platform/powermac/gmac_tree.c').read_text()
            for name, old, new, case, diagnostic in MUTATIONS:
                if product.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor is not unique')
                mutant = Path(temp) / 'gmac_tree.c'
                mutant.write_text(product.replace(old, new))
                failures = exercise(mutant, Path(temp), case)
                expected = [(case, 1, f'reached=A1\nH:A1 {diagnostic}\n'.encode())]
                if failures != expected:
                    raise RuntimeError(f'{name}: expected {expected}, got {failures}')
                print(f'RED: {name}: H:A1 {diagnostic}, rc=1, reached=A1')
    return 0


if __name__ == '__main__':
    sys.exit(main())
