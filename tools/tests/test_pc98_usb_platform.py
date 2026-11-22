"""PC-98 OHCI probe consumes only enumerated resources (USB_DESIGN 4-1)."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='pc98-usb-platform-host', tier=1, needs=[], budget=3,
             covers=['platform/pc98/usb_platform.c', 'platform/pc98/usb_probe.h',
                     'tools/tests/pc98_usb_platform_host.c', 'tools/tests/test_pc98_usb_platform.py',
                     'drivers/pci/pci.h', 'drivers/pci/pci_decode.h', 'drivers/pci/pci_barsize.h',
                     'include/pci_contract.h', 'include/pci_dev_contract.h', 'include/usb_platform.h',
                     'drivers/usb/ehci_quiesce.c', 'drivers/usb/ehci_handoff.c', 'drivers/usb/*.h', 'include/endian_dma.h',
                     'arch/x86/arch_endian_dma.h', 'include/kprintf.h', 'include/types.h', 'include/callconv.h', 'sdk/include/os32/os32_*.h'],
             basis=['docs/decisions/usb/USB_DESIGN.md §4-1'])
MUTATIONS = [
    ('dev->progif != 0x10', '0', 2),
    ('bar.prefetch || bar.unassigned', 'bar.prefetch', 3),
    ('dev->device != 0x0035', '0', 6),
    ('polls++ == 500', 'polls++ == 499', 9),
    ('if (take_ownership(hc) == 0)', 'if (take_ownership(hc) <= 0)', 8),
    ('ptr + EHCI_LEGCTLSTS + 1', 'ptr + EHCI_LEGCTLSTS', 8),
    ('ptr + EHCI_LEGCTLSTS, 0', 'ptr + EHCI_LEGCTLSTS + 1, 0', 8),
    ('disable:\n    usbp_cfg_cmd', 'disable:\n    if (0) usbp_cfg_cmd', 12),
    ('int rc = usbp_cfg_cmd(&hc.pci, 0, PCI_CMD_BUS_MASTER | PCI_CMD_MEM_ENABLE)',
     'int rc = usbp_cfg_cmd(&hc.pci, 0, PCI_CMD_BUS_MASTER)', 12),
    ('ptr + EHCI_OS_BYTE', 'ptr', 8),
    ('if (blocked) blocked_slots', 'if (!blocked) blocked_slots', 11),
    ('.fn = dev->fn', '.fn = 0', 1),
]


def run(ctx):
    root = Path(ctx.root)
    paths = ['platform/pc98/usb_platform.c', 'drivers/usb/ehci_handoff.c']
    originals = [(root / path).read_text() for path in paths]
    with tempfile.TemporaryDirectory() as directory:
        sources = [Path(directory) / Path(path).name for path in paths]
        exe = Path(directory) / 'probe'
        cases = [(originals, 0)]
        if ctx.mutate:
            for old, new, expected in MUTATIONS:
                assert sum(s.count(old) for s in originals) == 1, old
                cases.append(([s.replace(old, new) for s in originals], expected))
        for contents, expected in cases:
            for src, content in zip(sources, contents):
                src.write_text(content)
            subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-Iarch/x86', '-I.', '-Iinclude', '-Iplatform/pc98', '-Isdk/include/os32',
                            *sources, 'drivers/usb/ehci_quiesce.c', 'tools/tests/pc98_usb_platform_host.c', '-o', exe],
                           cwd=root, check=True)
            result = subprocess.run([exe]).returncode
            assert result == expected, (result, expected)
            print('RED' if expected else 'PASS', result)
    return 0
