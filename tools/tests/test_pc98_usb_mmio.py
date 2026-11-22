#!/usr/bin/env python3
"""PC-98 USB BAR reservation, shared UC mapping and boot rejection cases."""
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'platform/pc98/pci_mmio.c'
HOST = 'tools/tests/pc98_usb_mmio_host.c'
CHECK = dict(name='pc98-usb-mmio-host', tier=1, needs=[], budget=15,
             covers=['platform/pc98/pci_mmio.c', 'tools/tests/pc98_usb_mmio_host.c', 'tools/tests/test_pc98_usb_mmio.py',
                     'platform/pc98/pci_resources.h', 'drivers/pci/*.h', 'drivers/pci/pci_decode.c', 'mm/*.h',
                     'include/*.h', 'tools/tests/host_arch/*.h',
                     'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
             basis=['USB_DESIGN §5-3'])
MUTANTS = [('p->users > 1', '0'),
           ('p->users > 1', 'p->users >= 1'),
           ('if (r.base < hi', 'if (0 && r.base < hi'),
           ('if (bar.unassigned)', 'if (0)')]


def run(ctx):
    source = (ROOT / SOURCE).read_text()
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for pair in [None] + (MUTANTS if ctx.mutate else []):
            code = source
            if pair:
                old, new = pair
                assert code.count(old) == 1
                code = code.replace(old, new)
            (tmp / 'pci_mmio.c').write_text(code)
            exe = tmp / 'mmio'
            subprocess.run(['gcc', '-m32', '-ffreestanding', '-nostdlib', '-fno-pie', '-no-pie',
                            '-I' + td, '-Itools/tests/host_arch', '-Iplatform/pc98',
                            *include_flags(ROOT), HOST, 'drivers/pci/pci_decode.c', '-o', str(exe)], check=True, cwd=ROOT)
            rc = host32.run([str(exe)]).returncode
            if pair and not 0 < rc < 128 or not pair and rc:
                raise RuntimeError(f'{pair}: rc={rc}')
            print('RED runtime' if pair else 'GREEN runtime', pair or '', rc)
    return 0
