"""T7a-2e product direct low path and real reservation/allocator. CR3/TLB are modeled; memory at VA 0 is real.
qemu -B reserves a host offset so Linux mmap_min_addr does not hide page zero.
Normal control always precedes compiled, runtime-RED single omissions.
"""
CHECK = dict(name='v86-low-host', tier=1,
    covers=['tools/tests/font_guard_inactive.h', 'kernel/time/sysclk.c', 'drivers/input/kbd.c', 'tools/tests/test_shm_user.py', 'tools/tests/mutpar.py', 'mm/paging.*', 'include/platform_memory.h', 'kernel/v86/v86_mem.*', 'kernel/v86/v86_bios.*', 'kernel/v86/v86_snapshot.c', 'kernel/v86/v86.h', 'kernel/v86/v86_module.h', 'kernel/module/module.h', 'mm/pgalloc.h', 'arch/x86/kernel/idt.h', 'kernel/kselftest.h', 'kernel/v86/v86_io.h', 'exec/v86_call.c', 'include/**', 'arch/x86/**', 'platform/pc98/**', 'gfx/gfx.h', 'drivers/block/loop_dev.h', 'fs/vfs.h', 'lib/kstring.h', 'sdk/include/os32/**', 'tools/tests/v86_low_host.c', 'tools/tests/kernel_layout.py', 'tools/tests/host32.py', 'tools/tests/host_arch/**', 'exec/exec.c', 'drivers/video/pc98/backend_pegc.c', 'gfx/gfx_core.c', 'init/kernel.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shm.c', 'kernel/v86/v86.c', 'tools/tests/shm_user_host.c', 'mm/memory_boot.c', 'tools/tests/pgalloc_host_fixture.h', 'tools/gen_memmap.py', 'docs/02_memory.md', 'boot/loader_fat_new.asm', 'build/os32.ld', 'fs/fd_redirect.c', 'fs/pipe_buffer.c', 'arch/x86/kernel/kentry.asm', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/mkshlib.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'include/platform_timer.h', 'platform/pc98/clock.c', 'include/platform_emergency.h', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py'],
    needs=['make:all'], budget=3.989154)

from kernel_includes import include_flags
from pathlib import Path
import subprocess
import sys
import tempfile
from kernel_layout import linker_flags
import host32
from test_shm_user import function

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('mm/paging.c', 'page_tables[0][i] = saved;',
     'if (i + 1 < V86_GUEST_MAP_END / PAGE_SIZE) page_tables[0][i] = saved;', 'last-PTE'),
    ('mm/paging.c', '    arch_mmu_flush_tlb();\n    for (u32 i = 0; i < V86_GUEST_MAP_END / PAGE_SIZE; i++)',
     '    for (u32 i = 0; i < V86_GUEST_MAP_END / PAGE_SIZE; i++)', 'restore-TLB'),
    ('kernel/v86/v86_mem.c', '    kmemcpy(P2V(MEM_AUTOPLAY_MAILBOX_BASE), v86_call_mailbox(),\n            MEM_AUTOPLAY_MAILBOX_SIZE);', '', 'mailbox-restore'),
    ('kernel/v86/v86_mem.c', '    kmemset(P2V(V86_REMAP_START), 0, V86_REMAP_END);', '', 'fresh-zero'),
    ('kernel/v86/v86_mem.c', '        for (;;) _stop();', '', 'restore-fail-stop'),
    ('kernel/v86/v86_mem.c', 'platform_fault_cell(col, (u8)line[col], TATTR_RED);', '(void)line[col];', 'restore-diagnostic'),
    ('mm/paging.c', 'paging_set_not_present(0, MEM_NULL_GUARD_END);',
     'paging_set_readonly(0, MEM_NULL_GUARD_END);', 'page0-NP'),
    ('kernel/v86/v86_mem.c',
     'if (paging_v86_map_range(0, PAGE_SIZE, 0, PAGE_RW | PTE_USER) != 0)\n        goto fail;',
     'if (paging_v86_map_range(0, PAGE_SIZE, 0, PAGE_RW | PTE_USER) != 0)\n        (void)0;', 'page0-prepare-failure'),
    ('kernel/v86/v86_mem.c',
     'if (paging_v86_map_range(PAGE_SIZE, V86_REMAP_END, PAGE_SIZE,\n                             PAGE_RW | PTE_USER) != 0) goto fail;',
     'if (paging_v86_map_range(PAGE_SIZE, V86_REMAP_END, PAGE_SIZE,\n                             PAGE_RW | PTE_USER) != 0) (void)0;', 'low-prepare-failure'),
) + tuple(
    ('kernel/v86/v86_mem.c', '                goto fail;',
     f'                if (mi != {index}) goto fail;', f'identity-prepare-{index}')
    for index in range(7)
)

LOW_ROW = """        { 0, MB_PFN(MEM_CONV_END),
          LEDGER_R_FIXED, LEDGER_OWNER_KERNEL, LEDGER_CACHE_WB, 0 },"""
MUTATIONS += (
    ('mm/memory_boot.c', LOW_ROW, LOW_ROW + """
    { 0x6A, 0x8A, LEDGER_R_SURFACE_BACKING, LEDGER_OWNER_BOOT, LEDGER_CACHE_WB, 0 },""", 'old-low-reservation'),
    ('mm/memory_boot.c', LOW_ROW, LOW_ROW.replace('LEDGER_OWNER_KERNEL', 'LEDGER_OWNER_BOOT'), 'low-owner'),
    ('mm/memory_boot.c', LOW_ROW, LOW_ROW.replace('MB_PFN(MEM_CONV_END)', 'MB_PFN(MEM_CONV_END) - 1'), 'low-end-PFN'),
    ('kernel/v86/v86_mem.c', 'paging_v86_map_range(PAGE_SIZE, V86_REMAP_END, PAGE_SIZE,',
     'paging_v86_map_range(PAGE_SIZE, V86_REMAP_END, MEM_POOL_BASE,', 'old-backing-alias'),
    ('kernel/v86/v86_mem.c', '    (void)owner;',
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
    paths = ('include/platform_emergency.h', 'platform/pc98/emergency.c', 'mm/paging.c', 'kernel/v86/v86_mem.c', 'kernel/v86/v86_bios.c', 'kernel/v86/v86_snapshot.c', 'kernel/time/sysclk.c', 'platform/pc98/clock.c', 'drivers/input/kbd.c', 'mm/memory_boot.c')
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
            (work / 'bios_consumer_slice.inc').write_text(function(parts['drivers/input/kbd.c'], 'kbd_bios_shift_peek'))
            memory = parts['mm/memory_boot.c']
            # L15 keeps the two-row table inside memory_boot_regions. Copy
            # that initializer into the host's corresponding local scope.
            start = memory.index('    const struct {', memory.index('static int memory_boot_regions('))
            end = memory.index('    u32 i;', start)
            (work / 'low_reservation.inc').write_text(
                memory[memory.index('#define MB_PFN'):memory.index('STATIC_ASSERT(MEM_SHELL_BAND_END')] +
                memory[start:end])
            exe = work / 'fixture'
            cmd = ['gcc', *linker_flags(), '-m32', '-march=i386', '-std=gnu11', '-O2',
                   '-Wall', '-Wextra', '-Werror', '-Wno-array-bounds', '-Wno-discarded-qualifiers', '-Wno-stringop-overflow',
                   '-DPHYSMEM_HOST_TEST=1', '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
                   '-fno-delete-null-pointer-checks', '-ffunction-sections', '-fdata-sections',
                   '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-I' + str(work)]
            cmd += (['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'tools/tests')] + include_flags(ROOT))
            subprocess.run(cmd + [str(ROOT / 'tools/tests/v86_low_host.c'), str(ROOT / 'mm/physmem.c'), '-o', str(exe)], check=True)
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


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_v86_low.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
