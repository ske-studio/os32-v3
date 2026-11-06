"""M2: actual paging/ledger transactions, privileged instruction seams only."""
CHECK = dict(name='paging-device-map-host', tier=1, budget=30.0,
             covers=['mm/paging.c', 'mm/paging.h', 'mm/paging_*.inc',
                     'mm/pgalloc.c', 'mm/pgalloc.h', 'mm/pgalloc_*.inc',
                     'mm/physmem.c', 'mm/physmem.h', 'include/memmap.h',
                     'tools/tests/test_paging_device_map.py',
                     'tools/tests/paging_device_map_host.c',
                     'tools/tests/pgalloc_host_fixture.h',
                     'tools/tests/paging_source.py',
                     'tools/tests/host_arch/arch_cpu.h',
                     'tools/tests/host_arch/arch_io.h',
                     'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
                     'build/config.mk'], needs=[])
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
from paging_source import paging_source

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'mm/paging_map.inc'
MUTATIONS = [
    ('map-np', 'if (entry & PTE_PRESENT) return -1;',
     'if (0) return -1;'),
    ('unmap-identity',
     'if ((entry & ~(PTE_ACCESSED | PTE_DIRTY)) !=\n'
     '                ((p << PAGE_SHIFT) | PAGE_RW | PTE_PCD)) return -1;',
     'if ((entry & ~(PTE_ACCESSED | PTE_DIRTY | PTE_FRAME_MASK)) !=\n'
     '                (PAGE_RW | PTE_PCD)) return -1;'),
    ('unmap-keep-registration', 'ledger_device_map_commit(slot, unmap ? 0 : &map);',
     'ledger_device_map_commit(slot, &map);'),
    ('all-pages-before-commit', 'p < end; p++) { /* device PTE preflight */',
     'p + 1 < end; p++) { /* device PTE preflight */'),
    ('irq-recheck',
     'if (device_map_preflight(owner, rid, first, end, unmap, &slot) != 0) {',
     'if (0) {'),
    ('failed-unmap-removes-registration',
     'if (device_map_preflight(owner, rid, first, end, unmap, &slot) != 0) {',
     'if (device_map_preflight(owner, rid, first, end, unmap, &slot) != 0) {\n'
     '        if (unmap) ledger_device_map_commit(slot, 0);'),
]


def main():
    original = (ROOT / PRODUCT).read_text()
    source = paging_source(ROOT / 'mm/paging.c')
    with tempfile.TemporaryDirectory(prefix='paging-device-map-') as name:
        tmp = Path(name)
        (tmp / 'pgalloc_source.c').write_text(paging_source(ROOT / 'mm/pgalloc.c'))
        cpu = (ROOT / 'tools/tests/host_arch/arch_cpu.h').read_text().replace(
            'static inline void arch_mmu_flush_tlb(void)\n{\n}',
            'static inline void arch_mmu_flush_tlb(void)\n{\n    host_flush();\n}')
        (tmp / 'arch_cpu.h').write_text(cpu)
        io = (ROOT / 'tools/tests/host_arch/arch_io.h').read_text().replace(
            '    _disable();', '    _disable();\n    host_irq_boundary();')
        (tmp / 'arch_io.h').write_text(io)
        exe = tmp / 'host'
        command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                   '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                   '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
                   '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                   '-DPHYSMEM_HOST_TEST=1', '-I' + str(tmp),
                   '-I' + str(ROOT / 'tools/tests/host_arch'),
                   '-I' + str(ROOT / 'tools/tests'), *include_flags(ROOT),
                   str(ROOT / 'tools/tests/paging_device_map_host.c'),
                   str(ROOT / 'mm/physmem.c'), '-o', str(exe)]

        def build(body):
            (tmp / 'paging_source.c').write_text(source.replace(original, body))
            subprocess.run(command, check=True)

        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            build(original)
            host32.run([str(exe)], runner=runner, check=True)
            print(f'GREEN runner={runner}', flush=True)
            if '--mutate' in sys.argv:
                for label, old, new in MUTATIONS:
                    assert original.count(old) == 1, (label, original.count(old))
                    build(original.replace(old, new))
                    result = host32.run([str(exe)], runner=runner,
                                        capture_output=True, text=True)
                    assert result.returncode == 1 and 'FAIL ' in result.stdout, (
                        label, result.returncode, result.stdout, result.stderr)
                    print(f'MUTATE runtime RED {label}: {result.stdout.strip()}', flush=True)
                    build(original)
                    host32.run([str(exe)], runner=runner, check=True)
                    print(f'RESTORED GREEN {label}', flush=True)


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_paging_device_map.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
