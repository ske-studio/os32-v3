#!/usr/bin/env python3
"""T2e e10a real paging/SHM/V86; record: tools/tests/shm_user_tdd.md."""
import argparse
import re
from pathlib import Path
import subprocess
import tempfile
from mutpar import run_ordered
import host32
def function(text, name):
    match = re.search(r'^(?:static )?(?:void|int) (?:__cdecl )?' + name + r'\([^)]*\)\n\{', text, re.M)
    assert match, name
    pos, depth = match.end(), 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[match.start():pos] + '\n'


ROOT = Path(__file__).resolve().parents[2]

MUTATIONS = (
    ('return-audit', 'kernel/v86_mem.c', '    kselftest_audit_v86_return();', '', 'FAIL V86 return audited'),
    ('return-gfx', 'kernel/v86_mem.c', '    gfx_v86_return();', '', 'FAIL audit after gfx return'),
    ('kill-gcap-release', 'kernel/v86_mem.c', '    if (v86_session.aborting) v86_gcap_release();', '', 'FAIL kill gcap release audited'),
    ('end-guest-iopl', 'kernel/v86_mem.c', 'flags = (v86_session.saved_eflags & ~EFLAGS_IF) | (flags & EFLAGS_IF);', 'flags = flags;', 'FAIL kill restores caller IOPL'),
    ('direct-kill-no-end', 'exec/exec.c', '    v86_session_end();\n    ring3_context_clear();\n    exec_finish', '    ring3_context_clear();\n    exec_finish', 'FAIL kill release before owner reclaim'),
    ('end-reentry', 'kernel/v86_mem.c', 'if (v86_session.closing) { irq_restore(flags); return; }', 'if (0) { irq_restore(flags); return; }', 'FAIL end recursion'),
    ('landing-if', 'exec/exec.c', '    _enable();\n    v86_session_end();', '    v86_session_end();\n    _enable();', 'FAIL release IF enabled'),
    ('restore-flush', 'kernel/paging.c', '        page_tables[0][i] = saved;', '        page_tables[0][i] = saved; arch_mmu_flush_tlb();', 'FAIL V86 active TLB'),
    ('restore-current-pde', 'kernel/paging.c', '        page_tables[0][i] = saved;', '        page_tables[0][i] = saved; if (saved & PTE_USER) ((u32 *)P2V(paging_current_cr3()))[0] |= PTE_USER;', 'FAIL unrelated current PDE unchanged'),
    ('int80-offset', 'kernel/v86.c', 'V86I_EIP, V86I_CS, V86I_EFLAGS, V86I_ESP, V86I_SS,\n                    v86_gcap_keep_if()', 'V86F_EIP, V86F_CS, V86F_EFLAGS, V86F_ESP, V86F_SS,\n                    v86_gcap_keep_if()', 'FAIL int80 IVT target'),

    ('exec-gate', 'exec/exec.c', '    if (paging_v86_session_open()) return EXEC_ERR_INVALID;', '', 'FAIL session exec gate'),
    ('resume-gate', 'exec/exec.c', '    if (paging_v86_session_open()) return OS32_ERR_BUSY;', '', 'FAIL session exec gate'),
    ('kill-no-release', 'exec/exec.c', '    v86_session_end();\n    exec_finish(id, EXEC_ERR_FAULT, kind);', '    exec_finish(id, EXEC_ERR_FAULT, kind);', 'FAIL kill release before owner reclaim'),
    ('release-on-exception', 'kernel/v86_mem.c', ' || kctx_irq_depth || kctx_exc_depth', '', 'FAIL backing release trusted context'),
    ('page0-short', 'kernel/v86_bios.c', '#define REAL_SNAPSHOT_SIZE PAGE_SIZE', '#define REAL_SNAPSHOT_SIZE 0x600', 'FAIL V86 page0 all bytes'),
    ('kill-no-end', 'exec/exec.c', '    v86_session_end();\n    ring3_context_clear();\n    g_pending_id', '    ring3_context_clear();\n    g_pending_id', 'FAIL kill ends session before app jump'),
    ('fixed-restore', 'kernel/paging.c', '        page_tables[0][i] = saved;', '        page_tables[0][i] = i * PAGE_SIZE | PAGE_RW; (void)saved;', 'FAIL unmap before backing free'),
    ('active-pde', 'kernel/paging.c', '    ((u32 *)P2V(s->active_cr3))[0] = s->active_pde;', '', 'FAIL V86 active PDE exact'),
    ('session-gate', 'kernel/paging.c', '!v86_map_session || base >= end', 'base >= end', 'FAIL V86 outside session unchanged'),
    ('restore-count', 'kernel/paging.c', 'if (v86_map_session != s) bad++;', 'if (v86_map_session != s) (void)0;', 'FAIL V86 rejected restore counted'),
    ('db-unreserved', 'kernel/shm.c',
     '        shm_state[i] = SHM_RESERVED;',
     '', 'FAIL first allocation excludes DB'),
    ('db-shifted', 'kernel/shm.c',
     '        shm_state[i] = SHM_RESERVED;',
     '        shm_state[i + 1] = SHM_RESERVED;', 'FAIL first allocation excludes DB'),
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
    ('v86-restore', 'kernel/paging.c', '    page_directory[0] = s->master_pde;', '    page_directory[0] &= ~PTE_USER;', 'FAIL V86 restores PDE0 USER'),
    ('boot-order', 'kernel/kernel.c', '    kselftest_run();',
     '    kselftest_run();', 'FAIL boot before AS'),
    ('launch-TVRAM', 'exec/exec.c', '        /* --- K3:',
     '        paging_addrspace_map_user_range(ctx->as, TVRAM_CHAR_BASE, GVRAM_BRG_END, PAGE_RW | PTE_USER);\n        /* --- K3:',
     'FAIL launch TVRAM removed'),
    ('launch-font', 'exec/exec.c', '        /* --- K3:',
     '        paging_addrspace_map_user_range(ctx->as, MEM_FONT_CACHE_BASE, MEM_UNICODE_TABLE_BASE, PAGE_RW | PTE_USER);\n        /* --- K3:',
     'FAIL launch font removed'),
    ('launch-shm', 'exec/exec.c', '        /* --- K3:',
     '        paging_addrspace_map_user_range(ctx->as, MEM_SHM_BASE, MEM_SHM_BASE + MEM_SHM_SIZE, PAGE_RW | PTE_USER);\n        /* --- K3:',
     'FAIL launch SHM removed'),
)


def wiring(sources):
    e = sources['exec/exec.c']
    for name, guard in (('exec_launch', 'if (paging_v86_session_open()) return EXEC_ERR_INVALID;'),
                        ('exec_resume', 'if (paging_v86_session_open()) return OS32_ERR_BUSY;')):
        start = e.index('\n{', e.index(name + '('))
        assert e[start:].split('\n', 3)[2].strip() == guard, 'FAIL session exec gate'
    k = sources['kernel/kernel.c']
    boot = 'paging_boot_user_shared(exec_tramp_page_addr())'
    assert boot in k and k.index('shm_init();') < k.index(boot) < k.index('kselftest_run();'), 'FAIL boot before AS'
    assert k.count(boot) == 1, 'FAIL boot once'
    e = sources['exec/exec.c']
    launch = e[e.index('        /* 3 領域を張り終えて'):e.index('/* --- K3:')]
    assert 'TVRAM_CHAR_BASE' not in launch, 'FAIL launch TVRAM removed'
    assert 'MEM_FONT_CACHE_BASE' not in launch, 'FAIL launch font removed'
    assert 'MEM_SHM_BASE' not in launch, 'FAIL launch SHM removed'
    assert 'ring3_tramp_page' not in launch, 'FAIL launch trampoline removed'


def run(runner, mutation=None):
    paths = ('kernel/paging.c', 'kernel/shm.c', 'kernel/v86_mem.c', 'kernel/kernel.c', 'exec/exec.c', 'kernel/v86.c', 'kernel/v86_bios.c')
    sources = {p: (ROOT/p).read_text() for p in paths}
    if mutation:
        name, path, old, new, expected = mutation
        assert old in sources[path], name + ': mutation target'
        if name == 'boot-order':
            sources[path] = sources[path].replace('    shm_init();', '    shm_init();\n    kselftest_run();', 1)
        elif name in ('owned-user', 'db-unreserved', 'db-shifted', 'kill-no-end'):
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
        (work/'v86_bios_slice.inc').write_text(re.search(r'^#define REAL_SNAPSHOT_SIZE .*$', sources['kernel/v86_bios.c'], re.M)[0] + '\n' + (function(sources['kernel/v86_bios.c'], 'v86_bios_save_real') + function(sources['kernel/v86_bios.c'], 'v86_bios_restore_real')).replace('P2V(0)', 'host_page0'))
        (work/'v86_unwind_slice.inc').write_text(function(sources['kernel/v86.c'], 'v86_runtime_end') + function(sources['exec/exec.c'], 'exec_pending_transfer') + function(sources['exec/exec.c'], 'exec_pending_finish') + function(sources['exec/exec.c'], 'exec_exit') + function(sources['exec/exec.c'], 'ring3_kill_kind'))
        (work/'v86_int_slice.inc').write_text((function(sources['kernel/v86.c'], 'v86_do_int') + function(sources['kernel/v86.c'], 'v86_int80')).replace('(const u16 *)(vector * 4)', '(const u16 *)(host_page0 + vector * 4)'))
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
                  ('tools/tests/host_arch', 'tools/tests', 'include', 'arch/x86', 'platform/pc98', 'kernel', 'lib', 'exec', 'sdk/include/os32')]
        exe = work/'fixture'
        result = host32.build(cmd + [str(ROOT/'tools/tests/shm_user_host.c'), str(ROOT/'kernel/physmem.c'), '-o', str(exe)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError('build failure is not RED\n'+result.stderr)
        result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
        if mutation:
            assert result.returncode == 1 and expected in result.stdout, (name, result.returncode, result.stdout, result.stderr)
            print('RED', name, expected)
        else:
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
            print(result.stdout.strip())


@host32.control_session
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    host32.begin_control(args.mutate, args.runner, ROOT)
    with host32.control(args.mutate, args.runner, ROOT) as normal:
        if normal:
            run(args.runner)
    if args.mutate:
        for _ in run_ordered(lambda mutation: run(args.runner, mutation), MUTATIONS):
            pass
        print(f'PASS {len(MUTATIONS)}/{len(MUTATIONS)} mutations')

if __name__ == '__main__':
    main()
