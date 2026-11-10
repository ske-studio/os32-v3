#!/usr/bin/env python3
"""Exercise the product DMA pool in freestanding ILP32 processes."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path

CHECK = dict(name='ppc-lan-p12', tier=2, budget=60,
             covers=['platform/powermac/gmac_dma.c', 'tools/ppc/test_lan_p12.py',
                     'tools/ppc/lan_p12_fake.c'],
             inputs=['platform/powermac/lan_boot.h', 'platform/powermac/ppc_memmap.h',
                     'drivers/net/gem/gem.h', 'mm/paging.h', 'include/types.h',
                     'arch/ppc/ppc_oea.h', 'arch/ppc/ppc_layout.h'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('sync', '    ppc_sync();', '    /* sync removed */', 0, 'H:A4 dma-unpublished'),
    ('sync-order', '    for (u32 off = 0; off < DMA_BLOCK; off += PPC_CACHE_LINE)\n        ppc_dcbf_line(pool + off);\n    ppc_sync();', '    ppc_sync();\n    for (u32 off = 0; off < DMA_BLOCK; off += PPC_CACHE_LINE)\n        ppc_dcbf_line(pool + off);', 0, 'H:A4 dma-unpublished'),
    ('pcd', 'pa, PAGE_RW | PTE_PCD)', 'pa, PAGE_RW)', 0, 'H:pcd'),
    ('bus', 'address = saved.dma_bus_base + off;', 'address = off;', 1, 'H:bus'),
    ('window', 'off >= saved.dma_bytes || bytes > saved.dma_bytes - off ||', '0 ||', 1, 'H:window'),
    ('once', '    if (attempted) {', '    if (0 && attempted) {', 0, 'H:N3'),
]


def check(source, temp, case):
    exe = temp / 'test'
    cmd = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
           '-nostdlib', '-static', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror',
           '-DCASE='+str(case), '-I.', '-Iinclude', '-Iarch/ppc', '-Iplatform/powermac',
           '-Imm', '-Ibuild/out', str(source), 'tools/ppc/lan_p12_fake.c', '-o', str(exe)]
    built = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    if built.returncode:
        raise RuntimeError('compile: ' + built.stderr)
    return subprocess.run(['qemu-i386', str(exe)], cwd=ROOT, text=True, capture_output=True, timeout=30)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'platform/powermac/gmac_dma.c').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as directory:
        temp = Path(directory)
        source = temp / 'gmac_dma.c'
        source.write_text(original)
        for case in range(6):
            result = check(source, temp, case)
            if result.returncode or 'REACHED host' not in result.stdout.splitlines():
                raise RuntimeError('baseline case %d: %s' % (case, result.stdout + result.stderr))
        if args.mutate:
            for name, old, new, case, diagnostic in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError('nonunique anchor: ' + name)
                source.write_text(original.replace(old, new, 1))
                result = check(source, temp, case)
                if result.returncode != 1 or diagnostic not in result.stdout.splitlines() or 'REACHED host' not in result.stdout.splitlines():
                    raise RuntimeError('mutation %s: %s' % (name, result.stdout + result.stderr))
                print('RED', name, diagnostic)
    print('PASS ppc-lan-p12')


if __name__ == '__main__':
    main()
