"""T7a-2b direct low fixture. CR3/TLB are modeled; memory at VA 0 is real.
qemu -B reserves a host offset so Linux mmap_min_addr does not hide page zero.
Normal control always precedes compiled, runtime-RED single omissions.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
from kernel_layout import linker_flags
import host32
from test_shm_user import function

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('kernel/paging.c', 'page_tables[0][i] = saved;',
     'if (i + 1 < V86_GUEST_MAP_END / PAGE_SIZE) page_tables[0][i] = saved;', 'last-PTE'),
    ('kernel/paging.c', '    arch_mmu_flush_tlb();\n    for (u32 i = 0; i < V86_GUEST_MAP_END / PAGE_SIZE; i++)',
     '    for (u32 i = 0; i < V86_GUEST_MAP_END / PAGE_SIZE; i++)', 'restore-TLB'),
    ('kernel/v86_mem.c', '    kmemcpy(P2V(MEM_AUTOPLAY_MAILBOX_BASE), v86_call_mailbox(),\n            MEM_AUTOPLAY_MAILBOX_SIZE);', '', 'mailbox-restore'),
    ('kernel/v86_mem.c', '    kmemset(P2V(V86_REMAP_START), 0, V86_REMAP_END);', '', 'fresh-zero'),
    ('kernel/v86_mem.c', '        for (;;) _stop();', '', 'restore-fail-stop'),
    ('kernel/v86_mem.c', 'text[col] = (u8)line[col];', '', 'restore-diagnostic'),
    ('kernel/paging.c', 'paging_set_not_present(0, MEM_NULL_GUARD_END);',
     'paging_set_readonly(0, MEM_NULL_GUARD_END);', 'page0-NP'),
    ('kernel/v86_mem.c',
     'if (paging_v86_map_range(0, PAGE_SIZE, 0, PAGE_RW | PTE_USER) != 0)\n        goto fail;',
     'if (paging_v86_map_range(0, PAGE_SIZE, 0, PAGE_RW | PTE_USER) != 0)\n        (void)0;', 'page0-prepare-failure'),
    ('kernel/v86_mem.c',
     'if (paging_v86_map_range(PAGE_SIZE, V86_REMAP_END, PAGE_SIZE,\n                             PAGE_RW | PTE_USER) != 0) goto fail;',
     'if (paging_v86_map_range(PAGE_SIZE, V86_REMAP_END, PAGE_SIZE,\n                             PAGE_RW | PTE_USER) != 0) (void)0;', 'low-prepare-failure'),
) + tuple(
    ('kernel/v86_mem.c', '                goto fail;',
     f'                if (mi != {index}) goto fail;', f'identity-prepare-{index}')
    for index in range(7)
)

EXPECTED = {
    'last-PTE': 'restore must complete before caller return',
    'restore-TLB': 'TLB restored before caller',
    'mailbox-restore': 'mailbox restored',
    'fresh-zero': 'previous guest data zero',
    'restore-fail-stop': 'restore error fail-stop',
    'restore-diagnostic': 'restore TVRAM diagnostic',
    'page0-NP': 'normal page0 NP',
}



def main():
    paths = ('kernel/paging.c', 'kernel/v86_mem.c', 'kernel/v86_bios.c', 'kernel/v86_snapshot.c', 'kernel/sysclk.c', 'drivers/kbd.c')
    sources = {p: (ROOT / p).read_text() for p in paths}
    with tempfile.TemporaryDirectory(prefix='v86-low-') as td:
        work = Path(td)
        arch = (ROOT / 'tools/tests/host_arch/arch_cpu.h').read_text().replace(
            'static inline void arch_mmu_flush_tlb(void)\n{\n}',
            'static inline void arch_mmu_flush_tlb(void)\n{\n    flush_low();\n}')
        (work / 'arch_cpu.h').write_text(arch)
        def run(parts, label=None):
            for path, source in parts.items():
                (work / Path(path).name).write_text(source)
            (work / 'bios_consumer_slice.inc').write_text(function(parts['drivers/kbd.c'], 'kbd_bios_shift_peek'))
            exe = work / 'fixture'
            cmd = ['gcc', *linker_flags(), '-m32', '-march=i386', '-std=gnu11', '-O2',
                   '-Wall', '-Wextra', '-Werror', '-Wno-array-bounds', '-Wno-discarded-qualifiers', '-Wno-stringop-overflow',
                   '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
                   '-fno-delete-null-pointer-checks', '-ffunction-sections', '-fdata-sections',
                   '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-I' + str(work)]
            cmd += ['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'include',
                    'arch/x86', 'platform/pc98', 'kernel', 'lib', 'exec', 'gfx', 'fs', 'drivers', 'sdk/include/os32')]
            subprocess.run(cmd + [str(ROOT / 'tools/tests/v86_low_host.c'), '-o', str(exe)], check=True)
            result = subprocess.run(['qemu-i386', '-B', '0x100000000', str(exe)],
                                    capture_output=True, text=True, timeout=host32.RUN_TIMEOUT)
            assert result.returncode == (1 if label else 0), (label, result)
            assert ('FAIL ' + EXPECTED.get(label, 'every preparation map failure') if label else 'GREEN ') in result.stdout, result
            print(('RED runtime ' + label + ': ' if label else '') + result.stdout.strip(), flush=True)
        run(sources)
        if '--mutate' in sys.argv:
            for path, old, new, label in MUTATIONS:
                assert sources[path].count(old) == 1, label
                parts = dict(sources)
                parts[path] = parts[path].replace(old, new)
                run(parts, label)
    return 0

if __name__ == '__main__':
    sys.exit(main())
