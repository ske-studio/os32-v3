"""PCI configuration width, preservation, and validation tests."""
CHECK = dict(name='pci-hwb-host', tier=1, budget=3.4, needs=[], covers=[
    'drivers/pci/pci_hwb.c', 'drivers/pci/pci_hwb.h', 'drivers/pci/pci_decode.h',
    'kdebug/hwb_wire.h', 'tools/tests/pci_hwb_host.c', 'tools/tests/hwb_fake.h',
    'tools/tests/hwb_host.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py',
    'tools/tests/kernel_includes.py', 'include/types.h'])
import sys
import tempfile
from pathlib import Path
import hwb_host

FIXTURE = 'tools/tests/pci_hwb_host.c'
MUTATIONS = [
    ('write16-rmw', 'drivers/pci/pci_hwb.c', 'else if (width == 2) outpw(port, *value);',
     'else if (width == 2) { u32 old = inpd(PCI_CFG_DATA_PORT); outpd(PCI_CFG_DATA_PORT, (old & 0xffff0000u) | *value); }', 'w1c'),
    ('write16-lane', 'drivers/pci/pci_hwb.c', 'else if (width == 2) outpw(port, *value);',
     'else if (width == 2) outpw(PCI_CFG_DATA_PORT, *value);', 'write16-lane'),
    # A crossing access in a 256-byte space is necessarily unaligned. Bypass
    # both guards for that case to expose the boundary guard at runtime.
    ('cross-boundary', 'drivers/pci/pci_hwb.c',
     'width <= HW_PCI_CONFIG_BYTES - offset &&\n           !(offset & (width - 1))',
     '(width > HW_PCI_CONFIG_BYTES - offset || !(offset & (width - 1)))', 'boundary'),
    ('write-readback', 'drivers/pci/pci_hwb.c', 'if (width == 1) outp(port, *value);',
     'if (width == 1) { outp(port, *value); (void)inp(port); }', 'write-only'),
    ('reject-unregistered', 'drivers/pci/pci_hwb.c', 'if (!valid(bdf, offset, width) ||',
     'if (bdf == HW_BDF(0x7e, 0x1d, 7) || !valid(bdf, offset, width) ||', 'unregistered'),
    ('restore-cf8', 'drivers/pci/pci_hwb.c', 'outpd(PCI_CFG_ADDR_PORT, saved);', '(void)saved;', 'read-lane'),
    ('read8-lane', 'drivers/pci/pci_hwb.c', '*value = inp(port);', '*value = inp(PCI_CFG_DATA_PORT);', 'read-lane'),
    ('no-mechanism', 'drivers/pci/pci_hwb.c', '? HW_OK : HW_NOSYS;', '? HW_OK : HW_OK;', 'absent'),
]

def main():
    hwb_host.verify(FIXTURE)
    with tempfile.TemporaryDirectory(prefix='pci-hwb-cross-') as d:
        hwb_host.build(hwb_host.ROOT, 'drivers/pci/pci_hwb.c', Path(d) / 'pci.o', cross=True)
    print('product i386-elf compile PASS', flush=True)
    if '--mutate' in sys.argv:
        return int(bool(hwb_host.mutations(FIXTURE, MUTATIONS)))
    return 0

def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_pci_hwb.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)

if __name__ == '__main__':
    sys.exit(main())
