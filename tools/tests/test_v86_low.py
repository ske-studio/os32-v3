"""T7a-2e product direct low path and real reservation/allocator. CR3/TLB are modeled; memory at VA 0 is real.
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

LOW_ROW = """    { 0, MB_PFN(MEM_CONV_END),
      LEDGER_R_FIXED, LEDGER_OWNER_KERNEL, LEDGER_CACHE_WB, 0 },"""
MUTATIONS += (
    ('kernel/memory_boot.c', LOW_ROW, LOW_ROW + """
    { 0x6A, 0x8A, LEDGER_R_SURFACE_BACKING, LEDGER_OWNER_BOOT, LEDGER_CACHE_WB, 0 },""", 'old-low-reservation'),
    ('kernel/memory_boot.c', LOW_ROW, LOW_ROW.replace('LEDGER_OWNER_KERNEL', 'LEDGER_OWNER_BOOT'), 'low-owner'),
    ('kernel/memory_boot.c', LOW_ROW, LOW_ROW.replace('MB_PFN(MEM_CONV_END)', 'MB_PFN(MEM_CONV_END) - 1'), 'low-end-PFN'),
    ('kernel/v86_mem.c', 'paging_v86_map_range(PAGE_SIZE, V86_REMAP_END, PAGE_SIZE,',
     'paging_v86_map_range(PAGE_SIZE, V86_REMAP_END, MEM_POOL_BASE,', 'old-backing-alias'),
    ('kernel/v86_mem.c', '    (void)owner;',
     '    if (!pgalloc_alloc_phys(owner, 159)) return -2;', 'old-backing-allocation'),
)

EXPECTED = {
    'old-low-reservation': 'boot low reservation registration',
    'low-owner': 'single FIXED kernel WB all 160 PFNs',
    'low-end-PFN': 'single FIXED kernel WB all 160 PFNs',
    'old-backing-alias': 'direct physical PFN',
    'old-backing-allocation': 'session pool unchanged',
    'last-PTE': 'restore must complete before caller return',
    'restore-TLB': 'TLB restored before caller',
    'mailbox-restore': 'mailbox restored',
    'fresh-zero': 'previous guest data zero',
    'restore-fail-stop': 'restore error fail-stop',
    'restore-diagnostic': 'restore TVRAM diagnostic',
    'page0-NP': 'normal page0 NP',
}



def check_generated_map():
    sys.path.insert(0, str(ROOT / 'tools'))
    import gen_memmap
    m, _, rows = gen_memmap.load(str(ROOT), str(ROOT / 'build/out/kernel.map'))
    low = [r for r in rows if r['section'] == gen_memmap.CONV and r['start'] < m.get('MEM_CONV_END') and r['parent'] is None]
    assert len(low) == 1 and low[0]['name'] == 'V86_LOW'
    assert low[0]['start'] == 0 and low[0]['end'] == 160 * 4096 - 1
    assert low[0]['attr'] == 'FIXED/kernel/WB'
    for obsolete in ('MEM_FONT_CACHE_BASE', 'MEM_KCG_LZ4_TEMP_BASE', 'MEM_GFX_BB_BASE'):
        assert m.get(obsolete) is None, obsolete
    assert m.get('MEM_GFX_BB_SIZE') == 32 * 4096
    explanation = (ROOT / 'docs/02_memory.md').read_text()
    assert 'V86_LOW' in explanation and 'FIXED/kernel/WB' in explanation
    assert '160 PFN' in explanation and '636KiB' in explanation


def main():
    check_generated_map()
    paths = ('kernel/paging.c', 'kernel/v86_mem.c', 'kernel/v86_bios.c', 'kernel/v86_snapshot.c', 'kernel/sysclk.c', 'drivers/kbd.c', 'kernel/memory_boot.c')
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
            memory = parts['kernel/memory_boot.c']
            (work / 'low_reservation.inc').write_text(memory[memory.index('#define MB_PFN'):memory.index('STATIC_ASSERT(MEM_SHELL_BAND_END')])
            exe = work / 'fixture'
            cmd = ['gcc', *linker_flags(), '-m32', '-march=i386', '-std=gnu11', '-O2',
                   '-Wall', '-Wextra', '-Werror', '-Wno-array-bounds', '-Wno-discarded-qualifiers', '-Wno-stringop-overflow',
                   '-DPHYSMEM_HOST_TEST=1', '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
                   '-fno-delete-null-pointer-checks', '-ffunction-sections', '-fdata-sections',
                   '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-I' + str(work)]
            cmd += ['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'tools/tests', 'include',
                    'arch/x86', 'platform/pc98', 'kernel', 'lib', 'exec', 'gfx', 'fs', 'drivers', 'sdk/include/os32')]
            subprocess.run(cmd + [str(ROOT / 'tools/tests/v86_low_host.c'), str(ROOT / 'kernel/physmem.c'), '-o', str(exe)], check=True)
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
