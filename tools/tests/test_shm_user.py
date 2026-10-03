#!/usr/bin/env python3
"""T2e e10a real paging/SHM/V86; record: tools/tests/shm_user_tdd.md."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]

MUTATIONS = (
    ('lock-user', 'kernel/shm.c',
     'paging_shm_set_rw(addr, addr + (u32)span * SHM_BLOCK_SIZE, 0)',
     'paging_map_range(addr, addr + (u32)span * SHM_BLOCK_SIZE, addr, PAGE_RO)', 'FAIL lock USER'),
    ('free-user', 'kernel/shm.c',
     'paging_shm_set_rw(addr, addr + (u32)span * SHM_BLOCK_SIZE, 1)',
     'paging_map_range(addr, addr + (u32)span * SHM_BLOCK_SIZE, addr, PAGE_RW)', 'FAIL free USER'),
    ('owned-user', 'kernel/shm.c',
     'paging_shm_set_rw(blk_start, blk_start + SHM_BLOCK_SIZE, 1)',
     'paging_map_range(blk_start, blk_start + SHM_BLOCK_SIZE, blk_start, PAGE_RW)', 'FAIL owned USER'),
    ('cleanup-user', 'kernel/shm.c',
     '            if (paging_shm_set_rw(blk_start, blk_start + SHM_BLOCK_SIZE, 1) != 0)',
     '            if (paging_map_range(blk_start, blk_start + SHM_BLOCK_SIZE, blk_start, PAGE_RW) != 0)', 'FAIL cleanup USER'),
    ('generic-live', 'kernel/paging.c',
     'if (live_addrspaces && (flags & PTE_USER)) return -1;', '', 'FAIL generic set rejects USER'),
    ('boot-once', 'kernel/paging.c', 'boot_user_shared_done || live_addrspaces || !pg_enabled',
     'live_addrspaces || !pg_enabled', 'FAIL boot once'),
    ('shm-range', 'kernel/paging.c',
     'base < MEM_SHM_BASE || end > MEM_SHM_BASE + MEM_SHM_SIZE ||', '', 'FAIL SHM foreign USER range'),
    ('shm-user-missing', 'kernel/paging.c', 'if (!(entry & PTE_USER))', 'if (0)', 'FAIL missing USER atomic reject'),
    ('v86-restore', 'kernel/v86_mem.c', '    paging_v86_restore_shared_user();', '', 'FAIL V86 restores PDE0 USER'),
    ('boot-order', 'kernel/kernel.c', '    kselftest_run();',
     '    kselftest_run();', 'FAIL boot before AS'),
    ('launch-shm', 'exec/exec.c', '        /* --- K3:',
     '        paging_addrspace_map_user_range(ctx->as, MEM_SHM_BASE, MEM_SHM_BASE + MEM_SHM_SIZE, PAGE_RW | PTE_USER);\n        /* --- K3:',
     'FAIL launch SHM removed'),
)


def wiring(sources):
    k = sources['kernel/kernel.c']
    boot = 'paging_boot_user_shared(exec_tramp_page_addr())'
    assert boot in k and k.index('shm_init();') < k.index(boot) < k.index('kselftest_run();'), 'FAIL boot before AS'
    assert k.count(boot) == 1, 'FAIL boot once'
    e = sources['exec/exec.c']
    launch = e[e.index('/* VRAM (テキスト 0xA0000'):e.index('/* --- K3:')]
    assert 'MEM_SHM_BASE' not in launch, 'FAIL launch SHM removed'
    assert 'ring3_tramp_page' not in launch, 'FAIL launch trampoline removed'


def run(runner, mutation=None):
    paths = ('kernel/paging.c', 'kernel/shm.c', 'kernel/v86_mem.c', 'kernel/kernel.c', 'exec/exec.c')
    sources = {p: (ROOT/p).read_text() for p in paths}
    if mutation:
        name, path, old, new, expected = mutation
        assert old in sources[path], name + ': mutation target'
        if name == 'boot-order':
            sources[path] = sources[path].replace('    shm_init();', '    shm_init();\n    kselftest_run();', 1)
        elif name == 'owned-user':
            sources[path] = sources[path].replace(old, new, 1)
        else:
            sources[path] = sources[path].replace(old, new)
    try:
        wiring(sources)
    except AssertionError as error:
        if mutation and str(error) == expected:
            print('RED', name, expected)
            return
        raise
    with tempfile.TemporaryDirectory(prefix='e10a-shm-') as directory:
        work = Path(directory)
        arch = (ROOT/'tools/tests/host_arch/arch_cpu.h').read_text()
        arch = arch.replace('static inline void arch_mmu_flush_tlb(void)\n{\n}',
                            'static inline void arch_mmu_flush_tlb(void)\n{\n    host_flushes++; host_flushed_cr3 = host_cr3;\n}')
        (work/'arch_cpu.h').write_text(arch)
        for p in paths[:3]:
            source = sources[p]
            # Linux cannot dereference the V86 low alias; clear its backing instead.
            if p == 'kernel/v86_mem.c':
                source = source.replace('(volatile u32 *)(V86_REMAP_START + PAGE_SIZE)', '(volatile u32 *)P2V(backing_phys)')
            (work/Path(p).name).write_text(source)
        alloc = (ROOT/'kernel/pgalloc.c').read_text().replace('irq_save()', '0').replace('irq_restore(flags)', '(void)flags')
        (work/'pgalloc_host_source.c').write_text(alloc)
        cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
               '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static', '-no-pie',
               '-Wl,--gc-sections', '-DPHYSMEM_HOST_TEST=1']
        cmd += ['-I'+str(work)] + ['-I'+str(ROOT/p) for p in
                  ('tools/tests/host_arch', 'tools/tests', 'include', 'arch/x86', 'platform/pc98', 'kernel', 'lib', 'exec')]
        exe = work/'fixture'
        result = subprocess.run(cmd + [str(ROOT/'tools/tests/shm_user_host.c'), str(ROOT/'kernel/physmem.c'), '-o', str(exe)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError('build failure is not RED\n'+result.stderr)
        result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
        if mutation:
            assert result.returncode == 1 and expected in result.stdout, (name, result.returncode, result.stdout, result.stderr)
            print('RED', name, expected)
        else:
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
            print(result.stdout.strip())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    run(args.runner)
    if args.mutate:
        for mutation in MUTATIONS:
            run(args.runner, mutation)
        print(f'PASS {len(MUTATIONS)}/{len(MUTATIONS)} mutations (runtime 9, wiring 2)')

if __name__ == '__main__':
    main()
