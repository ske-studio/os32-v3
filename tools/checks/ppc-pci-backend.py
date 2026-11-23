"""PowerMac HW_OP §5 3b/6b/6c, including the R3-P1/P3 boot and guard cases."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='ppc-pci-backend', tier=1, needs=[], budget=10,
    covers=['platform/powermac/pci_backend.c', 'platform/powermac/pci_config.c',
            'platform/powermac/lan_boot.h', 'tools/ppc/pci_backend_fake.c',
            'platform/powermac/pci_boot_memory.inc', 'platform/powermac/kboot_info.h',
            'platform/powermac/pci_capture.h', 'kdebug/hwb_wire.h',
            'arch/ppc/kernel_trap.c', 'arch/ppc/ppc_trap.h', 'platform/powermac/hwb_instructions.h',
            'arch/ppc/ppc_oea.h', 'arch/ppc/ppc_boot.h', 'arch/ppc/kernel_boot.h',
            'include/types.h', 'include/callconv.h', 'include/memmap.h',
            'include/pci_config_contract.h', 'include/pci_bind_contract.h',
            'include/pci_dev_contract.h', 'drivers/pci/pci.h', 'drivers/pci/pci_bind.h',
            'include/pci_contract.h', 'include/kprintf.h', 'drivers/pci/*.c', 'drivers/pci/*.h', 'drivers/net/gem/gem.h',
            'sdk/include/os32/*.h', 'lib/kstring_c.c', 'lib/kstring.h'],
    basis=['docs/decisions/realhw/HWOP_COMMON_DESIGN.md §3', 'docs/decisions/ppc/PCI_COMMON_DESIGN.md §3・§7'])


def run(ctx):
    builtin = subprocess.check_output(['gcc', '-print-file-name=include'], text=True).strip()
    directory = ctx.root / 'platform/powermac'
    sources = {name: (directory / name).read_text()
               for name in ('pci_backend.c', 'pci_config.c')}
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / 'test'
        def check(expected, replacements):
            for name, source in sources.items():
                if name in replacements:
                    old, new = replacements[name]
                    if source.count(old) != 1:
                        raise ValueError('mutation anchor changed: ' + old)
                    source = source.replace(old, new)
                (Path(tmp) / name).write_text(source)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                            '-Werror', '-ffreestanding', '-nostdlib', '-no-pie', '-static',
                            '-nostdinc', '-isystem', builtin, '-ffunction-sections',
                             '-Wl,--gc-sections', '-DPM_PCI_FAKE_MMIO', '-DPPC_TRAP_HOST_TEST',
                            '-I.', '-Iinclude', '-Ilib', '-Iplatform/powermac', '-Iarch/ppc',
                            '-Isdk/include/os32', 'tools/ppc/pci_backend_fake.c',
                            'arch/ppc/kernel_trap.c', 'drivers/pci/pci.c', 'drivers/pci/pci_decode.c', 'drivers/pci/pci_barsize.c',
                            'drivers/pci/pci_bind.c', 'drivers/pci/pci_bind_match.c',
                            'lib/kstring_c.c', *(Path(tmp) / name for name in sources),
                            '-o', exe], cwd=ctx.root, check=True)
            rc = subprocess.run(['qemu-i386', exe], timeout=10).returncode
            if rc != expected:
                raise RuntimeError(f'expected assertion {expected}, got {rc}')

        check(0, {})
        if ctx.mutate:
            for label, name, old, new, expected in [
                ('command status mask', 'pci_config.c', 'reg == PCI_COMMAND ? 2 : 4',
                 '4', 9),
                ('command only', 'pci_backend.c', 'if (reg == 4)', 'if (1)', 8),
                ('shared isolation', 'pci_config.c', 'bridge->isolated = 1;', 'bridge->isolated = 0;', 23),
                ('occupied guard', 'pci_config.c', 'selected >= 0', 'selected >= -1', 22),
                ('command failure', 'pci_config.c', '&command)) return LAN_EIO;\n    if (rh_setup)', '&command)) return 0;\n    if (rh_setup)', 29),
                ('root index', 'pci_backend.c', 'idx != 0', 'idx > 1', 3),
                ('BAR writes', 'pci_backend.c', 'int pci_config_bar_measure(void)\n{\n    return 0;',
                 'int pci_config_bar_measure(void)\n{\n    return 1;', 4),
                ('unaligned read', 'pci_backend.c', 'reg & ~3u', 'reg', 5),
                ('byte order', 'pci_backend.c', 'return value;', 'return __builtin_bswap32(value);', 5),
                ('BDF range', 'pci_backend.c', 'dev > 31 || fn > 7 || ', '', 6),
            ]:
                check(expected, {name: (old, new)})
                print(f'RED {label} (assertion {expected})')
    print('PowerMac PCI backend OK' + (' (10 runtime mutants RED)' if ctx.mutate else ''))
    return 0
