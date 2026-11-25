"""PowerMac HW_OP §5 3b/6b/6c, including the R3-P1/P3 boot and guard cases."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='ppc-pci-backend', tier=1, needs=[], budget=10,
    covers=['sys/platform/powermac/pci_backend.c', 'sys/platform/powermac/pci_config.c',
            'sys/platform/powermac/lan_boot.h', 'tools/ppc/pci_backend_fake.c',
            'sys/platform/powermac/pci_boot_memory.inc', 'sys/platform/powermac/kboot_info.h',
            'sys/platform/powermac/pci_capture.h', 'sys/kern/kdebug/hwb_wire.h',
            'sys/arch/ppc/kernel_trap.c', 'sys/arch/ppc/ppc_trap.h', 'sys/platform/powermac/hwb_instructions.h',
            'sys/arch/ppc/ppc_oea.h', 'sys/arch/ppc/ppc_boot.h', 'sys/arch/ppc/kernel_boot.h',
            'sys/include/types.h', 'sys/include/callconv.h', 'sys/include/memmap.h',
            'sys/include/pci_config_contract.h', 'sys/include/pci_bind_contract.h',
            'sys/include/pci_dev_contract.h', 'sys/dev/pci/pci.h', 'sys/dev/pci/pci_bind.h',
            'sys/include/pci_contract.h', 'sys/include/kprintf.h', 'drivers/pci/*.c', 'drivers/pci/*.h', 'sys/dev/net/gem/gem.h',
            'sdk/include/os32/*.h', 'sys/kern/kstring_c.c', 'sys/kern/kstring.h'],
    basis=['docs/decisions/realhw/HWOP_COMMON_DESIGN.md §3', 'docs/decisions/ppc/PCI_COMMON_DESIGN.md §3・§7'])


def run(ctx):
    builtin = subprocess.check_output(['gcc', '-print-file-name=include'], text=True).strip()
    directory = ctx.root / 'platform/powermac'
    sources = {name: (directory / name).read_text()
               for name in ('pci_backend.c', 'pci_config.c')}
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / 'test'
        def check(expected, replacements, widths_only=False):
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
                             f'-DWIDTHS_ONLY={int(widths_only)}', '-Wl,--gc-sections', '-DPM_PCI_FAKE_MMIO', '-DPPC_TRAP_HOST_TEST',
                            '-I.', '-Iinclude', '-Ilib', '-Iplatform/powermac', '-Iarch/ppc',
                            '-Isdk/include/os32', 'tools/ppc/pci_backend_fake.c',
                            'sys/arch/ppc/kernel_trap.c', 'sys/dev/pci/pci.c', 'sys/dev/pci/pci_decode.c', 'sys/dev/pci/pci_barsize.c',
                            'sys/dev/pci/pci_bind.c', 'sys/dev/pci/pci_bind_match.c',
                            'sys/kern/kstring_c.c', *(Path(tmp) / name for name in sources),
                            '-o', exe], cwd=ctx.root, check=True)
            rc = subprocess.run(['qemu-i386', exe], timeout=10).returncode
            if rc != expected:
                raise RuntimeError(f'expected assertion {expected}, got {rc}')

        check(0, {})
        if ctx.mutate:
            anchor = 'u32 flags = ppc_irq_save(), rc = HW_IO_ERROR;'
            rmw = """if (write && width == 2 && reg == 4) {
                u32 word;
                u32 rc = pm_pci_access(p,bdf,reg,4,0,&word);
                if (rc) return rc;
                word = (word & 0xffff0000u) | (tmp & 0xffff);
                return pm_pci_access(p,bdf,reg,4,1,&word);
            }"""
            check(0, {}, widths_only=True)
            check(36, {'pci_config.c': (anchor, rmw + anchor)}, widths_only=True)
            print('RED halfword RMW clears adjacent W1C status')
        if ctx.mutate:
            for label, name, old, new, expected in [
                ('target_bdf restriction', 'pci_config.c', 'if (!p || !p->bridge)',
                 'if (!p || !p->bridge || bdf != p->target_bdf)', 34),
                ('GEM selectable', 'pci_config.c', ' || !selectable(&saved_pci, b->bdf)', '', 32),
                ('GEM master clear', 'pci_config.c', 'command & ~PCI_MASTER', 'command', 31),
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
    print('PowerMac PCI backend OK' + (' (14 runtime mutants RED)' if ctx.mutate else ''))
    return 0
