#!/usr/bin/env python3
"""USB 1I: hardware-level OHCI model linked to the complete real USB stack."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

import host32
import test_usb_ohci
import test_usb_core

ROOT = Path(__file__).resolve().parents[2]
SOURCES = test_usb_ohci.SOURCES + [s + '.c' for s in test_usb_core.SOURCES] + [
    'hid_kbd.c', 'hid_kbd_map.c', 'hid_mouse.c']
CHECK = dict(name='usb-stack', tier=1, needs=[], budget=60, covers=[
    'drivers/usb/*.[ch]', 'include/usb_platform.h', 'include/types.h',
    'include/callconv.h', 'include/endian_le.h', 'include/endian_dma.h',
    'arch/*/arch_endian_dma.h', 'lib/kstring.h',
    'tools/tests/test_usb_stack.py', 'tools/tests/usb_stack_host.c',
    'tools/tests/usb_fake_ohci.c', 'tools/tests/usb_fake_dev.c',
    'tools/tests/usb_fake_ehci_min.c', 'tools/tests/test_usb_ohci.py',
    'tools/tests/test_usb_core.py', 'tools/tests/host32.py', 'tools/tests/cc_cache.py'])
# Each anchor is a unique product line, never a stand-in for core behavior.
MUTATIONS = [
    ('ep0-capacity', 'ohci_sched.c', '    n = control ? 3u :',
     '    n = control ? (x->buf.size ? 3u : 2u) :'),
    ('F4', 'usb_service.c',
     '        p = hc->isr_pending | hc->ops->intr_collect(hc); hc->isr_pending = 0;',
     '        p = hc->isr_pending; hc->isr_pending = 0;'),
    ('F6r', 'usb_xfer.c', '    x->curr_set ^= 1;', '    x->curr_set ^= 0;'),
    ('F9', 'usb_xfer.c', '        x->did_dma_delay = 1; x->state = XS_DMA_DELAY;',
     '        x->did_dma_delay = 1; x->state = XS_CB_PENDING;'),
    ('F9d', 'usb_xfer.c', '    x->did_dma_delay = 0;', '    x->did_dma_delay = x->did_dma_delay;'),
    ('G1-pending', 'usb_xfer.c', '    while (*p && hc->state == HC_RUNNING) {',
     '    while (*p) {'),
    ('attached', 'usb_hc_dead.c', '            iface->attached = 0;', '            /* attached left set */'),
]
FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wvla', '-O2',
         '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector']


def command(argv):
    subprocess.run(argv, cwd=ROOT, check=True)


def build(tmp, tree, swap):
    exe = tmp / 'stack'
    command(['gcc', '-m32', *FLAGS, '-no-pie', '-nostdlib', '-static',
             '-Wl,-e,_start', '-Wl,--wrap=usb_dmamem_free',
             '-I' + str(tmp / 'fake-arch'), '-I' + str(tree / 'include'),
             '-I' + str(tree), *(['-DUSB_TEST_SWAP'] if swap else []),
             str(tree / 'tools/tests/usb_stack_host.c'),
             *[str(tree / 'drivers/usb' / s) for s in SOURCES], '-o', str(exe)])
    return exe


CASES = list(range(12))
CASE_NAMES = ['preconfigured HID/hub A5/A6/F11/F14/C6', 'F6r/F9/F9d',
              'G1(a)', 'G1(b)', 'G1(c)', 'G1(d)', 'G1(e)',
              'G1(f)', 'G1(g)', 'G1(h)', 'C6 attach BUSY death', 'natural A4']


def execute(exe, cases=CASES):
    results = []
    for case in cases:
        rc = host32.run([str(exe), str(case)], runner='qemu', timeout=15).returncode
        assert rc in (0, 1), ('crash/timeout', case, rc)
        results.append(rc)
        print('case', case, CASE_NAMES[case], 'rc=' + str(rc), flush=True)
    return results


def cross_compile(tmp, tree):
    for env, default, name, arch in [
            ('CROSS_DIR', '/home/hight/opt/cross', 'i386-elf-gcc', 'x86'),
            ('USB_PPC_CROSS_DIR', os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'),
             'powerpc-elf-gcc', 'ppc')]:
        compiler = Path(os.environ.get(env, default)) / 'bin' / name
        if not compiler.is_file():
            print('SKIP cross compile:', compiler, flush=True)
            continue
        for rel in ['tools/tests/usb_stack_host.c', 'drivers/usb/usb_hc_dead.c']:
            command([str(compiler), *FLAGS, '-I' + str(tmp / 'fake-arch'),
                     '-I' + str(tree / 'include'), '-I' + str(tree),
                     '-c', str(tree / rel), '-o', str(tmp / (arch + '.o'))])
        print('cross compile OK', arch, flush=True)


def check(mutate):
    start = time.monotonic()
    with tempfile.TemporaryDirectory(prefix='usb-stack-') as directory:
        tmp = Path(directory)
        tree = tmp / 'tree'
        for rel in ['drivers/usb', 'include', 'arch/x86', 'arch/ppc']:
            shutil.copytree(ROOT / rel, tree / rel)
        (tree / 'lib').mkdir()
        shutil.copyfile(ROOT / 'lib/kstring.h', tree / 'lib/kstring.h')
        (tree / 'tools/tests').mkdir(parents=True)
        for name in ['usb_stack_host.c', 'usb_fake_ohci.c']:
            shutil.copyfile(ROOT / 'tools/tests' / name, tree / 'tools/tests' / name)
        fake = tmp / 'fake-arch'
        fake.mkdir()
        (fake / 'arch_endian_dma.h').write_text(
            '#define mmio_rd32_le usb_fake_native_rd\n'
            '#define mmio_wr32_le usb_fake_native_wr\n'
            '#ifdef __powerpc__\n'
            f'#include "{tree / "arch/ppc/arch_endian_dma.h"}"\n'
            '#else\n'
            f'#include "{tree / "arch/x86/arch_endian_dma.h"}"\n'
            '#endif\n'
            '#undef mmio_rd32_le\n#undef mmio_wr32_le\n'
            'u32 mmio_rd32_le(const volatile void *p);\n'
            'void mmio_wr32_le(volatile void *p, u32 v);\n')
        cross_compile(tmp, tree)
        for swap in [False, True]:
            assert not any(execute(build(tmp, tree, swap))), ('baseline', swap)
            print('GREEN', 'SWAP' if swap else 'LE', flush=True)
        if mutate:
            for name, rel, anchor, replacement in MUTATIONS:
                path = tree / 'drivers/usb' / rel
                source = path.read_text()
                assert source.count(anchor) == 1, (name, anchor)
                path.write_text(source.replace(anchor, replacement))
                try:
                    for swap in [False, True]:
                        results = execute(build(tmp, tree, swap))
                        assert any(results), ('SURVIVED', name, swap)
                        print('RED runtime', name, 'SWAP' if swap else 'LE', flush=True)
                finally:
                    path.write_text(source)
    print('usb-stack rc=0 seconds=%.2f' % (time.monotonic() - start), flush=True)


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    check(parser.parse_args().mutate)
