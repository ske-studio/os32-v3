#!/usr/bin/env python3
"""Exercise UniNorth transactions against a freestanding host32 bus."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'platform/powermac/pci_config.c').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        path = Path(tmp) / 'pci_config.c'
        path.write_text(source)
        cmd = ['gcc', '-m32', '-nostdinc', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
               '-nostdlib', '-static', '-Wall', '-Wextra', '-Werror', '-DPM_PCI_FAKE_MMIO', '-DPM_PCI_FAKE_LANE',
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
            ('lane', 'bridge->data_va + (reg & 7)', 'bridge->data_va', 10),
            ('onehot', '(1u << dev) | fn', '(dev << 11) | fn', 11),
            ('target-bdf', 'if (!p || !p->bridge)', 'if (!p || !p->bridge || bdf != p->target_bdf)', 13),
            ('w1c', 'reg == PCI_COMMAND ? 2 : 4', '4', 12),
            ('select-flush', '!access_lane(addr, 4, 0, &rb) &&', '', 11),
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
