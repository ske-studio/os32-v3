#!/usr/bin/env python3
"""Compile the PPC GEM adapter and exercise PCI master enable on ILP32."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

CHECK = dict(name='ppc-lan-p24c', tier=2, budget=60,
             covers=['platform/powermac/lan_gem_io.c', 'tools/ppc/test_lan_p24c.py'],
             inputs=['platform/powermac/lan_boot.h', 'drivers/net/gem/gem.h',
                     'arch/ppc/kernel_boot.h', 'platform/powermac/kboot_info.h',
                     'build/ppc-kboot.mk'])
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'platform/powermac/lan_gem_io.c'
PPC = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin'

HARNESS = r'''
#include "platform/powermac/lan_boot.h"
#include "drivers/net/gem/gem.h"
#include "arch/ppc/kernel_boot.h"
#include "platform/powermac/kboot_info.h"
static int reads, writes, mode;
static u32 written;
static struct ppc_boot_devices dev = { .gmac_va=0x123000, .pci_addr_va=0x456000, .pci_data_va=0x457000 };
static struct pm_kboot_info info = { .tb_hz=12345 };
const struct ppc_boot_devices *ppc_boot_devices(void) { return &dev; }
const struct pm_kboot_info *ppc_boot_info(void) { return &info; }
int ppc_dma_sync(void *p, u32 n, enum dma_sync op) { (void)p; (void)n; (void)op; return 0; }
int pm_pci_read32(struct pm_pci *p, u32 b, u32 r, u32 *v) {
    if (p->addr_va != 0x456000 || p->data_va != 0x457000 || p->bus_first != 2 || p->bus_last != 9 || p->target_bdf != 0x234 || b != 0x234 || r != 4) return -1;
    ++reads; if (mode == 1 || (mode == 3 && reads == 2)) return -1;
    *v = reads == 1 ? 0xabcd0121 : (mode == 4 ? 0 : 4); return 0;
}
int pm_pci_write32(struct pm_pci *p, u32 b, u32 r, u32 v) {
    (void)p; (void)b; (void)r; ++writes; written=v; return mode == 2 ? -1 : 0;
}
static int test(void) {
    struct pm_gmac_boot b = { .bus_first=2, .bus_last=9, .bdf=0x234 };
    struct gem_io io;
    if (pm_lan_gem_io(&b, &io) || io.tb_hz != 12345 || io.sync != ppc_dma_sync || !io.ctx || !io.read32 || !io.write32 || !io.tb) return 10;
    for (mode=0; mode<5; ++mode) {
        int rc; reads=0; writes=0; written=0;
        rc=io.master_enable(io.ctx);
        if (mode == 0 && (rc || reads != 2 || writes != 1 || written != 0x125)) return 11;
        if (mode == 1 && (!rc || reads != 1 || writes)) return 12;
        if (mode == 2 && (!rc || reads != 1 || writes != 1)) return 13;
        if (mode == 3 && (!rc || reads != 2 || writes != 1)) return 14;
        if (mode == 4 && (!rc || reads != 2 || writes != 1)) return 15;
    }
    return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
'''

def invoke(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=120, **kw)

def require(cmd):
    p = invoke(cmd)
    if p.returncode:
        raise RuntimeError(f'{cmd}: {p.stdout}')
    return p.stdout

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = SOURCE.read_text()
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        mk = d / 'flags.mk'
        mk.write_text('include build/ppc-kboot.mk\nprint-flags:\n\t@echo $(KB_FLAGS)\n')
        flags = shlex.split(require(['make', '-s', '-f', str(mk), 'print-flags']))
        def cross(src):
            f = d / 'source.c'; f.write_text(src)
            obj = d / 'source.o'
            require([str(PPC / 'powerpc-elf-gcc'), *flags, '-c', str(f), '-o', str(obj)])
            return obj
        def assembly(obj):
            return require([str(PPC / 'powerpc-elf-objdump'), '-d', str(obj)])
        obj = cross(original)
        symbols = set(re.findall(r'\bU\s+(\w+)', require([str(PPC / 'powerpc-elf-nm'), '-u', str(obj)])))
        expected = {'ppc_dma_sync', 'ppc_boot_info', 'ppc_boot_devices', 'pm_pci_read32', 'pm_pci_write32'}
        if symbols != expected: raise RuntimeError(f'unexpected undefined symbols: {symbols}')
        dis = assembly(obj)
        def check_asm(text):
            rd = re.search(r'<rd>:(.*?)(?=\n[0-9a-f]+ <|\Z)', text, re.S)
            wr = re.search(r'<wr>:(.*?)(?=\n[0-9a-f]+ <|\Z)', text, re.S)
            return bool(rd and wr and re.search(r'\blwbrx\b.*\beieio\b', rd[1], re.S) and re.search(r'\b(?:hw)?sync\b.*\bstwbrx\b.*\beieio\b', wr[1], re.S))
        if not check_asm(dis): raise RuntimeError('register instruction sequence mismatch')
        print('PPC compile, symbols and register instructions OK')
        def host(src):
            # Replace only instructions unavailable on the host; master remains product code.
            src = re.sub(r'__asm__ volatile\("lwbrx [^\n]*;', '(void)addr; value = 0;', src)
            src = re.sub(r'__asm__ volatile\("(?:sync; )?stwbrx [^\n]*;', '(void)addr; (void)value;', src)
            src = re.sub(r'__asm__ volatile\("mftbu %0" : "=r"\((hi|again)\)\);', r'\1 = 0;', src)
            src = re.sub(r'__asm__ volatile\("mftb %0" : "=r"\(lo\)\);', 'lo = 0;', src)
            f = d / 'host.c'; f.write_text(src + '\n' + HARNESS)
            exe = d / 'host'
            require(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-I.', '-Iinclude', '-Iplatform/powermac', '-Iarch/ppc', '-Isdk/include', '-Isdk/include/os32', str(f), '-o', str(exe)])
            return invoke(['qemu-i386', str(exe)]).returncode
        if host(original) != 0: raise RuntimeError('host baseline failed')
        print('host PCI cases OK')
        if args.mutate:
            mutations = [('sync', 'sync; stwbrx', 'stwbrx', None),
                         ('readback', 'return (command & PCI_MASTER) ? GEM_OK : GEM_EIO;', 'return GEM_OK;', 15),
                         ('status', '(command & 0xffffu) | PCI_MASTER', 'command | PCI_MASTER', 11)]
            for name, before, after, code in mutations:
                if original.count(before) != 1: raise RuntimeError('mutation anchor: ' + name)
                mutant = original.replace(before, after)
                if code is None:
                    if check_asm(assembly(cross(mutant))): raise RuntimeError('survived: ' + name)
                else:
                    rc = host(mutant)
                    if rc != code: raise RuntimeError(f'{name}: unexpected rc={rc}, expected={code}')
                print('RED ' + name)
    return 0

if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.TimeoutExpired) as exc:
        print('test error:', exc)
        raise SystemExit(1)
