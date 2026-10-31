from paging_source import paging_source
from kernel_boot_source import kernel_source
from kernel_includes import include_flags
from kernel_layout import linker_flags
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
    match = re.search(r'^(?:static )?(?:(?:void|int|i32|u8|u32) |const u8 \*)(?:__cdecl )?' + name + r'\([^)]*\)\n\{', text, re.M)
    assert match, name
    pos, depth = match.end(), 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[match.start():pos] + '\n'


ROOT = Path(__file__).resolve().parents[2]

# Keep the original three-guard mutant as one unique, contiguous source span.
_paging = paging_source(ROOT/'mm/paging.c')
_generic_start = _paging.index('int paging_set_page(')
_generic_end = _paging.index('    if (npages >', _generic_start)
GENERIC_LIVE = _paging[_generic_start:_generic_end]
GENERIC_GUARD = '    if (live_addrspaces && (flags & PTE_USER)) return -1;\n'
assert GENERIC_LIVE.count(GENERIC_GUARD) == 3

MUTATIONS = (
    ('return-audit', 'kernel/v86/v86_mem.c', '    kselftest_audit_v86_return();', '', 'FAIL V86 return audited'),
    ('return-gfx', 'kernel/v86/v86_mem.c', '    gfx_v86_return();', '', 'FAIL audit after gfx return'),
    ('kill-gcap-release', 'kernel/v86/v86_mem.c', '    if (v86_session.aborting && kdebug.ops.gcap_release) kdebug.ops.gcap_release();', '', 'FAIL kill gcap release audited'),
    ('end-guest-iopl', 'kernel/v86/v86_mem.c', 'flags = (v86_session.saved_eflags & ~EFLAGS_IF) | (flags & EFLAGS_IF);', 'flags = flags;', 'FAIL kill restores caller IOPL'),
    ('direct-kill-no-end', 'exec/exec.c', '    v86_session_end();\n    ring3_context_clear();\n    v86_call_cleanup();\n    exec_finish', '    ring3_context_clear();\n    v86_call_cleanup();\n    exec_finish', 'FAIL direct kill closes session'),
    ('end-reentry', 'kernel/v86/v86_mem.c', 'if (v86_session.closing) { irq_restore(flags); return; }', 'if (0) { irq_restore(flags); return; }', 'FAIL end recursion'),
    ('landing-if', 'exec/exec.c', '    _enable();\n    v86_session_end();', '    v86_session_end();\n    _enable();', 'FAIL release IF enabled'),
    ('restore-flush', 'mm/paging.c', '        page_tables[0][i] = saved;', '        page_tables[0][i] = saved; arch_mmu_flush_tlb();', 'FAIL V86 active TLB'),
    ('restore-current-pde', 'mm/paging.c', '        page_tables[0][i] = saved;', '        page_tables[0][i] = saved; if (saved & PTE_USER) ((u32 *)P2V(paging_current_cr3()))[0] |= PTE_USER;', 'FAIL unrelated current PDE unchanged'),
    ('int80-offset', 'kernel/v86/v86.c', 'V86I_EIP, V86I_CS, V86I_EFLAGS, V86I_ESP, V86I_SS,\n                    (kdebug.ops.gcap_keep_if ? kdebug.ops.gcap_keep_if() : 0)', 'V86F_EIP, V86F_CS, V86F_EFLAGS, V86F_ESP, V86F_SS,\n                    (kdebug.ops.gcap_keep_if ? kdebug.ops.gcap_keep_if() : 0)', 'FAIL int80 IVT target'),

    ('exec-gate', 'exec/exec.c', '    if (paging_v86_session_open()) return EXEC_ERR_INVALID;', '', 'FAIL session exec gate'),
    ('resume-gate', 'exec/exec.c', '    if (paging_v86_session_open()) return OS32_ERR_BUSY;', '', 'FAIL session exec gate'),
    ('kill-no-release', 'exec/exec.c', '    v86_session_end();\n    v86_call_cleanup();\n    exec_finish(id, EXEC_ERR_FAULT, kind);', '    exec_finish(id, EXEC_ERR_FAULT, kind);', 'FAIL kill release before owner reclaim'),
    ('release-on-exception', 'kernel/v86/v86_mem.c', 'if (kctx_irq_depth || kctx_exc_depth) goto done;', '', 'FAIL gcap release normal context'),
    ('page0-forget', 'kernel/v86/v86_mem.c', '    v86_bios_restore_real();', '    v86_bios_restore_real();\n    v86_session.real_saved = 0;', 'FAIL V86 second original valid'),
    ('consumer-direct', 'gfx/gfx_core.c', 'v86_bios_real_lowmem()[PEGC_BIOS_ARCH_FLAG]', '*(volatile u8 *)P2V_IO(PEGC_BIOS_ARCH_FLAG)', 'FAIL BIOS gfx candidate immutable'),
    ('page0-short', 'kernel/v86/v86_snapshot.c', '#define REAL_SNAPSHOT_SIZE PAGE_SIZE', '#define REAL_SNAPSHOT_SIZE 0x600', 'FAIL V86 original immutable'),
    ('kill-no-end', 'exec/exec.c', '    v86_session_end();\n    ring3_context_clear();\n    g_pending_id', '    ring3_context_clear();\n    g_pending_id', 'FAIL kill ends session before app jump'),
    ('fixed-restore', 'mm/paging.c', '        page_tables[0][i] = saved;', '        page_tables[0][i] = i * PAGE_SIZE | PAGE_RW; (void)saved;', 'FAIL unexpected restore fail-stop'),
    ('active-pde', 'mm/paging.c', '    ((u32 *)P2V(s->active_cr3))[0] = s->active_pde;', '', 'FAIL unexpected restore fail-stop'),
    ('session-gate', 'mm/paging.c', '!v86_map_session || base >= end', 'base >= end', 'FAIL V86 outside session unchanged'),
    ('restore-count', 'mm/paging.c', 'if (v86_map_session != s) bad++;', 'if (v86_map_session != s) (void)0;', 'FAIL restore mismatch must fail-stop'),
    ('db-unreserved', 'mm/shm.c',
     '         i < SHM_DB_BLOCK_FIRST + SHM_DB_BLOCK_COUNT; i++) {\n        shm_state[i] = SHM_RESERVED;',
     '         i < SHM_DB_BLOCK_FIRST + SHM_DB_BLOCK_COUNT; i++) {', 'FAIL first allocation excludes DB'),
    ('db-shifted', 'mm/shm.c',
     '         i < SHM_DB_BLOCK_FIRST + SHM_DB_BLOCK_COUNT; i++) {\n        shm_state[i] = SHM_RESERVED;',
     '         i < SHM_DB_BLOCK_FIRST + SHM_DB_BLOCK_COUNT; i++) {\n        shm_state[i + 1] = SHM_RESERVED;', 'FAIL first allocation excludes DB'),
    ('lock-user', 'mm/shm.c',
     'paging_shm_set_rw(addr, addr + (u32)span * SHM_BLOCK_SIZE, 0)',
     'paging_map_range(addr, addr + (u32)span * SHM_BLOCK_SIZE, addr, PAGE_RO)', 'FAIL lock USER'),
    ('free-user', 'mm/shm.c',
     'paging_shm_set_rw(addr, addr + (u32)span * SHM_BLOCK_SIZE, 1)',
     'paging_map_range(addr, addr + (u32)span * SHM_BLOCK_SIZE, addr, PAGE_RW)', 'FAIL free USER'),
    ('owned-user', 'mm/shm.c',
     '\n        if (paging_shm_set_rw(blk_start, blk_start + SHM_BLOCK_SIZE, 1) != 0)',
     '\n        if (paging_map_range(blk_start, blk_start + SHM_BLOCK_SIZE, blk_start, PAGE_RW) != 0)', 'FAIL owned USER'),
    ('cleanup-user', 'mm/shm.c',
     '            if (paging_shm_set_rw(blk_start, blk_start + SHM_BLOCK_SIZE, 1) != 0)',
     '            if (paging_map_range(blk_start, blk_start + SHM_BLOCK_SIZE, blk_start, PAGE_RW) != 0)', 'FAIL cleanup USER'),
    ('generic-live', 'mm/paging.c',
     GENERIC_LIVE, GENERIC_LIVE.replace(GENERIC_GUARD, ''), 'FAIL generic set rejects USER'),
    ('boot-once', 'mm/paging.c', 'boot_user_shared_done || live_addrspaces || !pg_enabled',
     'live_addrspaces || !pg_enabled', 'FAIL boot once'),
    ('shm-range', 'mm/paging.c',
     'if (base < MEM_SHM_BASE || end > MEM_SHM_BASE + MEM_SHM_SIZE ||',
     # Accept the foreign range before the SHM audit bitmap is indexed.
     # Deleting the guard now crashes there instead of reaching the assertion.
     'if (base < MEM_SHM_BASE || end > MEM_SHM_BASE + MEM_SHM_SIZE) return 0;\n    if (',
     'FAIL SHM foreign USER range'),
    ('shm-user-missing', 'mm/paging.c', 'if (!(entry & PTE_USER))', 'if (0)', 'FAIL missing USER atomic reject'),
    ('v86-restore', 'mm/paging.c', '    page_directory[0] = s->master_pde;', '    page_directory[0] &= ~PTE_USER;', 'FAIL unexpected restore fail-stop'),
    ('boot-order', 'init/kernel.c', '    if (kdebug.state == KDEBUG_RUNNING) kdebug.ops.selftest_run();',
     '    if (kdebug.state == KDEBUG_RUNNING) kdebug.ops.selftest_run();', 'FAIL boot before AS'),
    ('launch-TVRAM', 'exec/exec.c', '        /* --- K3:',
     '        paging_addrspace_map_user_range(ctx->as, TVRAM_CHAR_BASE, GVRAM_BRG_END, PAGE_RW | PTE_USER);\n        /* --- K3:',
     'FAIL launch TVRAM removed'),
    ('launch-font', 'exec/exec.c', '        /* --- K3:',
     '        paging_addrspace_map_user_range(ctx->as, 0x01000UL, 0x4A000UL, PAGE_RW | PTE_USER);\n        /* --- K3:',
     'FAIL launch font removed'),
    ('launch-shm', 'exec/exec.c', '        /* --- K3:',
     '        paging_addrspace_map_user_range(ctx->as, MEM_SHM_BASE, MEM_SHM_BASE + MEM_SHM_SIZE, PAGE_RW | PTE_USER);\n        /* --- K3:',
     'FAIL launch SHM removed'),
)


def wiring(sources):
    e = sources['exec/exec.c']
    for name, guard in (('exec_launch', 'if (paging_v86_session_open()) return EXEC_ERR_INVALID;'),
                        ('exec_resume', 'if (paging_v86_session_open()) return OS32_ERR_BUSY;')):
        body = function(e, name)
        start = body.index('\n{')
        # Both refusals precede any state change; font was added in T7b-a2.
        first = [line.strip() for line in body[start:].split('\n')[2:4]]
        assert first == ['if (font_guard_active) return OS32_ERR_BUSY;', guard], 'FAIL session exec gate'
    k = kernel_source(ROOT, sources)
    boot = 'paging_boot_user_shared(exec_tramp_page_addr())'
    assert boot in k and k.index('shm_init();') < k.index(boot) < k.index('kdebug.ops.selftest_run();'), 'FAIL boot before AS'
    assert k.count(boot) == 1, 'FAIL boot once'
    e = function(sources['exec/exec.c'], 'exec_launch')
    # f12 removed the tier bookkeeping after the three private mappings.
    # Keep checking that interval through the shlib attach, using live calls.
    start = e.index('        if (app_map_region(ctx->as, load_base, sbrk_end)')
    launch = e[start:e.index('        if (shlib_addrspace_attach(ctx->as) < 0)', start)]
    assert 'TVRAM_CHAR_BASE' not in launch, 'FAIL launch TVRAM removed'
    assert '0x01000UL' not in launch, 'FAIL launch font removed'
    assert 'MEM_SHM_BASE' not in launch, 'FAIL launch SHM removed'
    assert 'ring3_tramp_page' not in launch, 'FAIL launch trampoline removed'


def run(runner, mutation=None):
    paths = ('mm/paging.c', 'mm/shm.c', 'kernel/v86/v86_mem.c', 'init/kernel.c', 'exec/exec.c', 'kernel/v86/v86.c', 'kernel/v86/v86_snapshot.c', 'drivers/video/pc98/backend_pegc.c', 'gfx/gfx_core.c')
    sources = {p: paging_source(ROOT/p) for p in paths}
    if mutation:
        name, path, old, new, expected = mutation
        assert sources[path].count(old) == 1, name + ': mutation target'
        if name == 'boot-order':
            assert sources[path].count(old) == sources[path].count('    shm_init();') == 1
            sources[path] = sources[path].replace('    shm_init();', '    shm_init();\n' + new, 1)
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
        (work/'platform_emergency.h').write_text(
            (ROOT/'include/platform_emergency.h').read_text())
        (work/'v86_bios_slice.inc').write_text(re.search(r'^#define REAL_SNAPSHOT_SIZE .*$', sources['kernel/v86/v86_snapshot.c'], re.M)[0] + '\n' + (function(sources['kernel/v86/v86_snapshot.c'], 'v86_bios_save_real') + function(sources['kernel/v86/v86_snapshot.c'], 'v86_bios_restore_real') + function(sources['kernel/v86/v86_snapshot.c'], 'v86_bios_real_lowmem')).replace('P2V(0)', 'host_page0').replace('P2V_BOOT(0)', 'host_page0'))
        consumer = function(sources['drivers/video/pc98/backend_pegc.c'], 'bios_flag')
        consumer += function(sources['drivers/video/pc98/backend_pegc.c'], 'pegc_identify')
        consumer += function(sources['gfx/gfx_core.c'], 'gfx_identify_candidates')
        (work/'bios_consumer_slice.inc').write_text(consumer.replace('P2V_IO(PEGC_BIOS_ARCH_FLAG)', '(host_page0 + PEGC_BIOS_ARCH_FLAG)'))
        (work/'v86_unwind_slice.inc').write_text(function(sources['kernel/v86/v86.c'], 'v86_runtime_end') + function(sources['exec/exec.c'], 'exec_pending_transfer') + function(sources['exec/exec.c'], 'exec_pending_finish') + function(sources['exec/exec.c'], 'exec_exit') + function(sources['exec/exec.c'], 'ring3_kill_kind'))
        (work/'v86_int_slice.inc').write_text((function(sources['kernel/v86/v86.c'], 'v86_do_int') + function(sources['kernel/v86/v86.c'], 'v86_int80')).replace('(const u16 *)(vector * V86_IVT_ENTRY_BYTES)', '(const u16 *)(host_page0 + vector * V86_IVT_ENTRY_BYTES)'))
        arch = (ROOT/'tools/tests/host_arch/arch_cpu.h').read_text()
        arch = arch.replace('static inline void arch_mmu_flush_tlb(void)\n{\n}',
                            'static inline void arch_mmu_flush_tlb(void)\n{\n    host_flushes++; host_flushed_cr3 = host_cr3;\n}')
        (work/'arch_cpu.h').write_text(arch)
        for p in paths[:3]:
            source = sources[p]
            # Linux low bytes are modeled here; v86_low_host executes real low RAM.
            if p == 'kernel/v86/v86_mem.c':
                source = source.replace('P2V(V86_REMAP_START)', 'host_low').replace('P2V(MEM_AUTOPLAY_MAILBOX_BASE)', '(host_low + MEM_AUTOPLAY_MAILBOX_BASE)')
            (work/Path(p).name).write_text(source)
        alloc = paging_source(ROOT/'mm/pgalloc.c')
        alloc = alloc.replace('irq_save()', '0').replace('irq_restore(flags)', '(void)flags')
        (work/'pgalloc_host_source.c').write_text(alloc)
        cmd = ['gcc', *linker_flags(), '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
               '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static', '-no-pie',
               '-Wl,--gc-sections', '-DPHYSMEM_HOST_TEST=1']
        cmd += ['-I'+str(work)] + (['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'tools/tests')] + include_flags(ROOT))
        exe = work/'fixture'
        result = host32.build(cmd + [str(ROOT/'tools/tests/shm_user_host.c'), str(ROOT/'mm/physmem.c'), '-o', str(exe)], capture_output=True, text=True)
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
    parser.add_argument('--mutation', action='append', choices=[m[0] for m in MUTATIONS])
    args = parser.parse_args()
    host32.begin_control(args.mutate, args.runner, ROOT)
    with host32.control(args.mutate, args.runner, ROOT) as normal:
        if normal:
            run(args.runner)
    if args.mutate:
        for _ in run_ordered(lambda mutation: run(args.runner, mutation), [m for m in MUTATIONS if not args.mutation or m[0] in args.mutation]):
            pass
        print('PASS selected mutations')

if __name__ == '__main__':
    main()
