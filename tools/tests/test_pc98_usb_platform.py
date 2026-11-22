"""PC-98 OHCI probe consumes only enumerated resources (USB_DESIGN 4-1)."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='pc98-usb-platform-host', tier=1, needs=[], budget=3,
             covers=['platform/pc98/usb_platform.c', 'platform/pc98/usb_probe.h',
                     'tools/tests/pc98_usb_platform_host.c', 'tools/tests/test_pc98_usb_platform.py',
                     'drivers/pci/pci.h', 'drivers/pci/pci_decode.h', 'drivers/pci/pci_barsize.h',
                     'include/pci_contract.h', 'include/pci_dev_contract.h', 'include/usb_platform.h',
                     'include/types.h', 'include/callconv.h', 'sdk/include/os32/os32_*.h'],
             basis=['docs/decisions/usb/USB_DESIGN.md §4-1'])
MUTATIONS = [
    ('dev->progif != 0x10', '0', 2),
    ('bar.unassigned ||', '', 3),
    ('.fn = dev->fn', '.fn = 0', 1),
]


def run(ctx):
    root = Path(ctx.root)
    original = (root / 'platform/pc98/usb_platform.c').read_text()
    with tempfile.TemporaryDirectory() as directory:
        src = Path(directory) / 'probe.c'
        exe = Path(directory) / 'probe'
        cases = [(original, 0)]
        if ctx.mutate:
            for old, new, expected in MUTATIONS:
                assert original.count(old) == 1
                cases.append((original.replace(old, new), expected))
        for source, expected in cases:
            src.write_text(source)
            subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-I.', '-Iinclude', '-Iplatform/pc98', '-Isdk/include/os32',
                            src, 'tools/tests/pc98_usb_platform_host.c', '-o', exe],
                           cwd=root, check=True)
            result = subprocess.run([exe]).returncode
            assert result == expected, (result, expected)
            print('RED' if expected else 'PASS', result)
    return 0
