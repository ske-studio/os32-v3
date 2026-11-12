#!/usr/bin/env python3
"""Exercise UniNorth transactions against a freestanding host32 bus."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-lan-p8', tier=1, budget=60, needs=[],
             covers=['platform/powermac/pci_config.c'],
             inputs=['tools/ppc/test_lan_p8.py', 'tools/ppc/test_lan_p8_fake.c',
                     'platform/powermac/lan_boot.h', 'arch/ppc/ppc_oea.h',
                     'arch/ppc/kernel_boot.h', 'arch/ppc/ppc_boot.h',
                     'drivers/net/gem/gem.h'])

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'platform/powermac/pci_config.c').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        path = Path(tmp) / 'pci_config.c'
        path.write_text(source)
        cmd = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
               '-nostdlib', '-static', '-Wall', '-Wextra', '-Werror', '-DPM_PCI_FAKE_MMIO',
               '-I.', '-Iplatform/powermac', '-Iarch/ppc', '-Iinclude', str(path),
               'tools/ppc/test_lan_p8_fake.c', '-o', tmp + '/test']
        def check():
            subprocess.run(cmd, cwd=ROOT, check=True, timeout=60)
            return subprocess.run(['qemu-i386', tmp + '/test'], timeout=60, capture_output=True, text=True)
        result = check()
        if result.returncode:
            raise RuntimeError(f'control failed: {result.returncode} {result.stdout}')
        print('control OK')
        # The exit code identifies the precise assertion reached by each mutant.
        mutations = [
            ('lane-read', '*value = le_read(p->data_va + (reg & 4u));', '*value = le_read(p->data_va + (reg & 0u));', 10),
            ('lane-write', 'le_write(p->data_va + (reg & 4u), *value);', 'le_write(p->data_va + (reg & 0u), *value);', 12),
            ('onehot', '(1u << dev) | (fn << 8) | reg', '(bus << 16) | (dev << 11) | (fn << 8) | reg | 1u', 11),
            ('le', 'return __builtin_bswap32(value);', 'return value;', 10),
            ('w1c', '*value &= 0xffffu;', '*value &= 0xffffffffu;', 12),
            ('readback', '(command & PCI_MASTER) ? LAN_EIO : 0', '0', 18),
            ('id', 'id != b->vendor_device', '0', 16),
        ]
        if args.mutate:
            for name, old, new, expected in mutations:
                if source.count(old) != 1:
                    raise RuntimeError(f'{name}: ambiguous anchor')
                path.write_text(source.replace(old, new))
                result = check()
                if result.returncode != expected or f"reach {expected}\nfail {expected}\n" not in result.stdout:
                    raise RuntimeError(f'{name}: expected {expected}, got {result.returncode}: {result.stdout}')
                print(f'RED {name}: assertion {expected}')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
