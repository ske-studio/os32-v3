"""PC-98 boot probe, OHCI takeover, shared IRQ ownership and root exploration."""
from pathlib import Path
import subprocess
import tempfile

SOURCE = 'platform/pc98/usb_resident.c'
PLATFORM = 'platform/pc98/usb_platform.c'
BODY = 'platform/pc98/usb_module.c'
HOST = 'tools/tests/pc98_usb_resident_host.c'
CHECK = dict(name='pc98-usb-resident-host', tier=1, needs=[], budget=5,
             covers=['platform/pc98/usb_module.c', 'input/hal/*.h', 'platform/pc98/usb_resident.c', 'tools/tests/pc98_usb_resident_host.c', 'platform/pc98/usb_platform.c', 'platform/pc98/usb*.h',
                     'kernel/irq/*.h', 'kernel/module/*.h',
                     'drivers/pci/*.h', 'drivers/usb/*.h', 'include/*.h', 'arch/x86/*.h',
                     'sdk/include/os32/*.h', 'tools/tests/test_pc98_usb_resident.py'],
             basis=['docs/decisions/usb/USB_DESIGN.md §4-1, §7-3'])
MUTANTS = [('if (active) core->service()', 'core->service()', 1),
           ('OHCI_COMMAND_STATUS, OHCI_CMD_OCR', 'OHCI_COMMAND_STATUS, 0', 2),
           ('dispatch, slot, IRQ_F_SHARED', 'dispatch, slot, 0', 2),
           ('? IRQ_HANDLED : IRQ_NONE', '? IRQ_NONE : IRQ_HANDLED', 3),
           ('? IRQ_HANDLED : IRQ_NONE', '? IRQ_HANDLED : IRQ_HANDLED', 10),
           ('& PCI_CMD_BUS_MASTER) &&', '& 0) &&', 11),
           ('core->dead(hc, USB_WHY_START);', '(void)hc;', 6)]


def run(ctx):
    root = Path(ctx.root)
    originals = [(root / path).read_text() for path in (SOURCE, PLATFORM, BODY)]
    with tempfile.TemporaryDirectory() as tmp:
        sources = [Path(tmp) / Path(path).name for path in (SOURCE, PLATFORM, BODY)]
        exe = Path(tmp) / 'host'
        cases = [(False, '', '', 0), (True, '', '', 0)]
        if ctx.mutate:
            cases.extend((False, *mutation) for mutation in MUTANTS)
            cases.extend((True, *mutation) for mutation in [
                ('active < 0 ? active : 0', 'active', 22),
                ('isr_pending |= pending', 'isr_pending = pending', 23),
                ('return pending ?', 'return hc->isr_pending ?', 24),
                ('e.buttons = buttons', 'e.buttons = !buttons', 26)])
        for module, old, new, expected in cases:
            if old:
                assert sum(source.count(old) for source in originals) == 1, old
            for src, original in zip(sources, originals):
                src.write_text(original.replace(old, new) if old else original)
            subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffunction-sections', '-Wl,--gc-sections', '-I.', '-Iinclude',
                            '-Iarch/x86', '-Iplatform/pc98', '-Isdk/include/os32', *sources, HOST, '-o', exe], cwd=root, check=True)
            result = subprocess.run([exe, 'module'] if module else [exe]).returncode
            assert result == expected, (old, result, expected)
            print('RED runtime' if old else 'PASS', old, result)
    return 0
