#!/usr/bin/env python3
"""USB 1B: frame-driven OHCI/EHCI models, two byte orders and runtime mutants."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['usb_dmamem.c', 'ohci_init.c', 'usb_hc_dead.c', 'ohci_sched.c',
           'ohci_done.c', 'ohci_unlink.c', 'ohci_roothub.c', 'ehci_quiesce.c']
CHECK = dict(name='usb-ohci', tier=1, needs=[], budget=20,
             covers=['drivers/usb/*.[ch]', 'include/usb_platform.h',
                     'include/endian_dma.h', 'include/endian_le.h', 'include/types.h',
                     'include/callconv.h', 'arch/*/arch_endian_dma.h',
                     'tools/tests/test_usb_ohci.py', 'tools/tests/usb_fake_ohci.c',
                     'tools/tests/usb_fake_ehci_min.c', 'tools/tests/host32.py',
                     'tools/tests/cc_cache.py'])
# (acceptance, file, unique single-line anchor, replacement).
# F9/F9d and G1 service-side mutations exercise the minimal core stand-in;
# real 1C integration remains the PM's explicitly separate gate.
MUTATIONS = [
    ('R1', 'ohci_init.c', '            OHCI_WATCH);', '            OHCI_WATCH | (hc->has_irq ? OHCI_INTR_MIE : 0));'),
    ('A1-stop', 'ohci_init.c', '    ohci_wr(hc, OHCI_CONTROL, OHCI_CTL_RESET); ohci_wr(hc, OHCI_COMMAND_STATUS, OHCI_CMD_HCR);', '    /* stop writes removed */'),
    ('A3', 'ohci_init.c', '    tree_init(hc);', '    if (0) tree_init(hc);'),
    ('A7', 'usb_dmamem.c', '        if (!ohci_dma_page_ok(d)) { /* A7: independently validate HW2 */', '        if (0 && !ohci_dma_page_ok(d)) { /* A7: independently validate HW2 */'),
    ('F4', 'tools/tests/usb_fake_ohci.c', '    st = hc->ops->intr_collect(hc);', '    st = 0;'),
    ('F5', 'ohci_done.c', '    if (!st) return 0;', '    if (!st) return 1;'),
    ('F6', 'ohci_done.c', '    if (!(head & OHCI_HALTED) && ((head ^ tail) & OHCI_PTR_MASK)) return 0;', '    (void)tail; if ((head & OHCI_PTR_MASK) != p->last->bus) return 0;'),
    ('F6e', 'ohci_done.c', '        if (data) x->actual += cbp ? cbp - td->start_bus : td->len;', '        if (data || td->stage == TD_SETUP) x->actual += cbp ? cbp - td->start_bus : td->len;'),
    ('F6l', 'ohci_done.c', '        shrt = data && (cc == OHCI_CC_DATA_UNDERRUN || cbp);', '        shrt = data && td != p->last && (cc == OHCI_CC_DATA_UNDERRUN || cbp);'),
    ('F6s', 'ohci_done.c', '        if (td->hw_next_is_end && td->alt_next) {', '        if (td->alt_next) {'),
    ('F6m', 'ohci_sched.c', '            td_init(td, in ? OHCI_TD_IN : OHCI_TD_OUT, bus, len, TD_DATA);', '            td_init(td, in ? OHCI_TD_IN | OHCI_TD_ROUND : OHCI_TD_OUT, bus, len, TD_DATA);'),
    ('F6r', 'tools/tests/usb_fake_ohci.c', '    x->curr_set ^= 1;', '    x->curr_set ^= 0;'),
    ('F9', 'tools/tests/usb_fake_ohci.c', '        x->state = XS_DMA_DELAY;', '        x->state = XS_CB_PENDING;'),
    ('F9u', 'ohci_unlink.c', '    ed->sw_prev = NULL;', '    dma_le32_put(&ed->hw->nexted, 0); ed->sw_prev = NULL;'),
    ('F9d', 'tools/tests/usb_fake_ohci.c', '    x->did_dma_delay = 0;', '    x->did_dma_delay = x->did_dma_delay;'),
    ('F10', 'usb_dmamem.c', '        usbp_dma_free(&hc->dma[j]);', '        if (0) usbp_dma_free(&hc->dma[j]);'),
    ('G1-quarantine', 'usb_hc_dead.c', '        if (hc->dma[i].va) usbp_dma_quarantine(&hc->dma[i]);', '        if (hc->dma[i].va) usbp_dma_free(&hc->dma[i]);'),
    ('G1-once', 'usb_hc_dead.c', '    if (hc->state == HC_DEAD) return;', '    if (0 && hc->state == HC_DEAD) return;'),
    ('G1-dead-DMA', 'ohci_unlink.c', '    if (!p || hc->state == HC_DEAD) return;', '    if (!p) return;'),
    ('G1-dead-MMIO', 'ohci_roothub.c', '    if (hc->state != HC_RUNNING) return -USB_E_HC;', '    if (0 && hc->state != HC_RUNNING) return -USB_E_HC;'),
    ('G1-pending', 'tools/tests/usb_fake_ohci.c', '    if (x->state != XS_CB_PENDING || x->dev->hc->state != HC_RUNNING) return;', '    if (x->state != XS_CB_PENDING) return;'),
    ('G2', 'ohci_init.c', '    if (hc->started & OHCI_STARTED_BM) {', '    if (1 || (hc->started & OHCI_STARTED_BM)) {'),
    ('F2', 'ehci_quiesce.c', '    ohci_wr(hc, op + EHCI_CONFIGFLAG, 0);', '    ohci_wr(hc, op + EHCI_CONFIGFLAG, 1);'),
    ('F3', 'ehci_quiesce.c', '    ohci_wr(hc, op + EHCI_USBCMD, 0);', '    ohci_wr(hc, op + EHCI_USBCMD, EHCI_HCRESET);'),
]


def command(argv):
    subprocess.run(argv, cwd=ROOT, check=True)


def harness(tmp, tree, swap, ehci=False):
    exe = tmp / ('ehci' if ehci else 'ohci')
    srcs = ['ehci_quiesce.c'] if ehci else SOURCES[:-1]
    flags = ['-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wvla',
             '-O2', '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie',
             '-fno-stack-protector', '-nostdlib', '-Wl,-e,_start', '-Wl,--wrap=usb_dmamem_free',
             '-I' + str(tmp / 'fake-arch'), '-I' + str(tree / 'include'), '-I' + str(tree)]
    if swap:
        flags.append('-DUSB_TEST_SWAP')
    fixture = tree / 'tools/tests' / ('usb_fake_ehci_min.c' if ehci else 'usb_fake_ohci.c')
    command(['gcc', *flags, str(fixture),
             *[str(tree / 'drivers/usb' / s) for s in srcs], '-o', str(exe)])
    return host32.run([str(exe)], timeout=15).returncode


def check(mutate):
    start = time.monotonic()
    with tempfile.TemporaryDirectory(prefix='usb-ohci-') as directory:
        tmp = Path(directory)
        tree = tmp / 'tree'
        for rel in ['drivers/usb', 'include', 'arch/x86', 'arch/ppc']:
            shutil.copytree(ROOT / rel, tree / rel)
        (tree / 'tools/tests').mkdir(parents=True)
        for fixture in ['usb_fake_ohci.c', 'usb_fake_ehci_min.c']:
            shutil.copyfile(ROOT / 'tools/tests' / fixture, tree / 'tools/tests' / fixture)
        fake = tmp / 'fake-arch'
        fake.mkdir()
        (fake / 'arch_endian_dma.h').write_text(
            '#define mmio_rd32_le usb_fake_native_rd\n'
            '#define mmio_wr32_le usb_fake_native_wr\n'
            f'#include "{tree / "arch/x86/arch_endian_dma.h"}"\n'
            '#undef mmio_rd32_le\n#undef mmio_wr32_le\n'
            'u32 mmio_rd32_le(const volatile void *p);\n'
            'void mmio_wr32_le(volatile void *p, u32 v);\n')
        for swap in [False, True]:
            for ehci in [False, True]:
                rc = harness(tmp, tree, swap, ehci)
                assert rc == 0, ('baseline', swap, ehci, rc)
                print('GREEN', 'EHCI' if ehci else 'OHCI', 'SWAP' if swap else 'LE', flush=True)
        if mutate:
            for name, rel, anchor, replacement in MUTATIONS:
                path = tree / (rel if rel.startswith('tools/') else 'drivers/usb/' + rel)
                source = path.read_text()
                assert source.count(anchor) == 1, (name, anchor)
                path.write_text(source.replace(anchor, replacement))
                try:
                    for swap in [False, True]:
                        rc = harness(tmp, tree, swap, rel == 'ehci_quiesce.c')
                        assert rc in (1, 2, 3), (name, swap, 'not runtime RED', rc)
                        print('RED', name, 'SWAP' if swap else 'LE', 'rc=' + str(rc), flush=True)
                finally:
                    path.write_text(source)
    print(f'usb-ohci rc=0 seconds={time.monotonic() - start:.2f}', flush=True)


def run(ctx):
    argv = [sys.executable, '-B', 'tools/tests/test_usb_ohci.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    check(parser.parse_args().mutate)
