"""T2f f3/f4: real host-only appmem/paging_app/paging/pgalloc/physmem ILP32.
記録: tools/tests/appmem_map_tdd.md
Only intended runtime FAIL labels count as RED; errors/timeouts are separate.
"""
import argparse
import hashlib
import pathlib
import statistics
import subprocess
import tempfile
import time

import host32
from mutpar import run_ordered

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = ['exec/appmem.c', 'kernel/paging_app.c', 'exec/appmem_map.c', 'exec/appmem_unmap.c', 'kernel/paging.c', 'exec/exec.c', 'exec/appslot.c', 'kapi/kapi_generated.c', 'exec/exec_heap.c']
TARGET_SRCS = ['exec/exec_heap.c', 'exec/exec_heap.h', 'kernel/kmalloc.c', 'kernel/kmalloc.h', 'exec/redir_access.c', 'exec/access_walk.c', 'tools/tests/exec_heap_host.h', 'sdk/kapi.json', 'kapi/kapi_generated.c', 'exec/exec.c', 'exec/appslot.c', 'exec/lease.c', 'exec/appslot.h', 'exec/appmem.c', 'kernel/paging_app.c', 'exec/appmem_map.c', 'exec/appmem_unmap.c',
    'exec/appmem.h', 'kernel/paging_app.h', 'kernel/paging.c', 'kernel/paging.h',
    'kernel/pgalloc.c', 'kernel/pgalloc.h', 'kernel/physmem.c', 'kernel/physmem.h',
    'include/appmem_types.h', 'sdk/include/os32/os32_kapi_shared.h', 'include/types.h', 'include/memmap.h', 'include/io.h',
    'include/cpu.h', 'include/pc98.h', 'include/sys.h', 'include/tvram.h',
    'kernel/gdt.h', 'kernel/tss.h', 'arch/x86/arch_cpu.h', 'arch/x86/arch_io.h', 'lib/kstring.h', 'tools/tests/appmem_map_host.c',
    'tools/tests/test_appmem_map.py', 'tools/tests/pgalloc_host_fixture.h',
    'tools/tests/host_arch/arch_cpu.h', 'tools/tests/host_arch/arch_io.h',
    'tools/tests/host32.py', 'tools/tests/mutpar.py', 'platform/pc98/platform_io.h']
DATA_ABORT = '''    for (u32 va = tx->base; va < tx->end; va += PAGE_SIZE) {
        u32 *entry = app_entry(tx, va);
        if (entry && *entry) {
            app_return(tx->as, *entry & ~(PAGE_SIZE - 1U));
            *entry = 0;
        }
    }
'''
PT_ABORT = '''    for (u32 k = 0; k < MEM_APP_BAND_MAX_PDES; k++) if (tx->pending[k]) {
        app_return(tx->as, tx->pending[k]);
        tx->pending[k] = 0;
    }
'''
MUTANTS = [
    ('heap-rounded-growth-limit', 8, 'if (request >= MEM_EXEC_HEAP_MIN) return 0;',
     '(void)request; if (size >= MEM_EXEC_HEAP_MIN) return 0;', 'unaligned small growth'),
    ('heap-large-early-reject', 8, 'size > ~(u32)0 - (BLK_ALIGN - 1) - BLK_HDR_SIZE',
     'size >= MEM_EXEC_HEAP_MIN', 'large INITIAL allocation'),
    ('heap-CPL3-init', 5, 'if (want_ring3) exec_heap_user_init(ctx->as);\n        else if (exec_heap_size) exec_heap_init_at(exec_heap_base, exec_heap_size);',
     'if (exec_heap_size) exec_heap_init_at(exec_heap_base, exec_heap_size);', 'CPL3 init keeps resident'),
    ('heap-free-validation', 8, 'if (!user_view(as, 0, va, &view)) return;',
     'view = (KHeap){(u8 *)MEM_EXEC_HEAP_BASE, MEM_EXEC_HEAP_MIN, as->exec_heap_used, "exec_heap"};', 'corrupt free immutable'),
    ('heap-extent-kind', 8, 'if (!user_arena(e)) continue;',
     'if (!e->base) continue;', 'TOPDOWN separate arena'),
    ('heap-tail-exact', 8, '            va = next;',
     '            if (match) { va = e->end; break; } va = next;', 'corrupt free immutable'),
    ('heap-PTE-owner', 8, 'if (!ok) goto bad;', '(void)ok;', 'corrupt free immutable'),
    ('heap-cross-free', 8, 'if (!member) return;',
     'if (!member) { exec_heap_free(ptr); return; }', 'resident unchanged'),
    ('heap-R1', 8, 'if (kctx_irq_depth || kctx_exc_depth || !as || as->appmem_poisoned ||\n        as->exec_heap_used == ~(u32)0 || !size',
     'if (!as || as->appmem_poisoned ||\n        as->exec_heap_used == ~(u32)0 || !size', 'R1 USER alloc'),
    ('heap-CR3-routing', 7, 'if (caller_access_get(&caller) && caller.origin == CALLER_USER)\n        return exec_heap_user_alloc',
     'if (caller_access_get(&caller) && paging_current_cr3() != paging_kernel_pd_phys())\n        return exec_heap_user_alloc', 'WM resident'),
    ('heap-CPL3-restore', 5, 'if (!a->cpl3 && a->exec_heap_base != 0) {',
     'if (a->exec_heap_base != 0) {', 'CPL3 restore keeps resident'),
    ('teardown-ARENA', 5, 'if (e->kind == APPMEM_ANON || e->kind == APPMEM_EXEC_ARENA) {',
     'if (e->kind == APPMEM_ANON) {', 'teardown R5 no leftover'),
    ('unmap-public-ARENA', 3, 'return unmap_mask(as, table, base, bytes, APPMEM_PUBLIC_UNMAP_MASK);',
     'return unmap_mask(as, table, base, bytes, APPMEM_PUBLIC_UNMAP_MASK | APPMEM_KIND_MASK(APPMEM_EXEC_ARENA));', 'unmap reject before writes'),
    ('data-zero', 1, 'kmemset(P2V(phys), 0, PAGE_SIZE);', '(void)phys;', 'data zero'),
    ('free-before-publish', 2, 'paging_app_commit(&tx);', 'paging_app_abort(&tx); paging_app_commit(&tx);', 'no free before publication'),
    ('PT-zero', 1, 'kmemset(P2V(tx->pending[k]), 0, PAGE_SIZE);', '(void)k;', 'PT zero'),
    ('early-PRESENT', 1, 'phys | PTE_RW | PTE_USER;', 'phys | PAGE_RW | PTE_USER;', 'staged PRESENT clear'),
    ('rollback-PFN-leak', 1, 'app_return(tx->as, *entry & ~(PAGE_SIZE - 1U));', '(void)entry;', 'data before PT free'),
    ('rollback-PT-leak', 1, 'app_return(tx->as, tx->pending[k]);', '(void)k;', 'PFN rollback'),
    ('PT-before-data', 1, DATA_ABORT + PT_ABORT, PT_ABORT + DATA_ABORT, 'data before PT free'),
    ('failure-publish', 2, 'if (rc) return rc;\n    if (!appmem_plan_valid(table, &plan)) {', 'if (rc) { appmem_publish(table, &plan); return rc; }\n    if (!appmem_plan_valid(table, &plan)) {', 'failure table unchanged'),
    ('active-no-reload', 2, 'if (paging_current_cr3() == as->pd_phys) paging_load_cr3(as->pd_phys);', '(void)as;', 'publish reload count'),
    ('alloc-IRQ-disabled', 1, 'tx->as = as; tx->base = base; tx->end = end;', 'tx->as = as; tx->base = base; tx->end = end; _disable();', 'alloc IF enabled'),
    ('nonzero-overwrite', 1, 'if (entry && *entry) return APPMEM_EINVAL;', '(void)entry;', 'nonzero PTE rejected'),
    ('late-nonzero-only-first', 1, 'if (entry && *entry) return APPMEM_EINVAL;',
     'if (va == base && entry && *entry) return APPMEM_EINVAL;', 'late nonzero PTE rejected'),
    ('abort-whole-existing-PT', 1, PT_ABORT,
     '    for (u32 k = 0; k < MEM_APP_BAND_MAX_PDES; k++) if (tx->end > tx->base && tx->as->app_pt_phys[k]) kmemset(P2V(tx->as->app_pt_phys[k]), 0, PAGE_SIZE);\n' + PT_ABORT,
     'existing PT rollback unchanged'),
    ('postcommit-abort', 1, 'tx->end = tx->base;', '(void)tx;', 'no free before publication'),
    ('IRQ-pending-pages', 1, 'for (u32 k = first; k <= last; k++) if (!tx->pending[k]) {', 'for (u32 k = first; k <= last; k++) if (1) {', 'IRQ page work bounded'),
    ('unmap-FULL-partial', 3, '    if (rc) return rc;\n    if (!appmem_unmap_plan_valid(table, &plan)) return APPMEM_EINVAL;\n    rc = paging_app_unmap_prepare',
     '    if (rc) { if (rc == APPMEM_EFULL) { paging_app_unmap_prepare(&tx, as, base, base + bytes); unsigned int f = irq_save(); paging_app_unmap_withdraw(&tx); irq_restore(f); paging_app_unmap_free(&tx); } return rc; }\n    if (!appmem_unmap_plan_valid(table, &plan)) return APPMEM_EINVAL;\n    rc = paging_app_unmap_prepare',
     'unmap FULL no withdrawal'),
    ('unmap-other-owner', 1, 'if (!pgalloc_page_owned(pfn, owner)) return 0;',
     'if (0 && !pgalloc_page_owned(pfn, owner)) return 0;', 'unmap reject before writes'),
    ('unmap-EXEC-public', 3, 'APPMEM_PUBLIC_UNMAP_MASK', '~0U', 'unmap reject before writes'),
    ('unmap-free-before-TLB', 3, '    paging_app_unmap_withdraw(&tx);',
     '    if (paging_current_cr3() == as->pd_phys) { irq_restore(saved); paging_app_unmap_free(&tx); saved = irq_save(); } paging_app_unmap_withdraw(&tx);', 'unmap TLB before free'),
    ('unmap-no-reload', 1, 'if (paging_current_cr3() == tx->as->pd_phys) paging_load_cr3(tx->as->pd_phys);',
     '(void)tx;', 'unmap TLB before free'),
    ('unmap-early-zero', 1, '        if (!pgalloc_free_n_owner(tx->as->owner, *entry / PAGE_SIZE, 1)) {',
     '        *entry = 0; if (!pgalloc_free_n_owner(tx->as->owner, *entry / PAGE_SIZE, 1)) {', 'unmap owner argument'),
    ('unmap-zero-omitted', 1, '        *entry = 0;\n    }\n    u32 first',
     '        (void)entry;\n    }\n    u32 first', 'unmap returned entry zero'),
    ('unmap-PT-before-PDE', 1, 'if (unmap_empty(tx, k)) { pd[APP_BAND_PDE + k] = 0; continue; }',
     'if (unmap_empty(tx, k)) { pgalloc_free_n_owner(tx->as->owner, tx->as->app_pt_phys[k] / PAGE_SIZE, 1); pd[APP_BAND_PDE + k] = 0; continue; }', 'unmap free IF enabled'),
    ('unmap-live-PT-free', 1, 'if (empty) {', 'if (1) { (void)empty;', 'unmap only empty PT'),
    ('unmap-PT-metadata', 1, '        tx->as->app_pt_phys[k] = 0;',
     '        (void)k;', 'unmap context restored'),
    ('unmap-hole', 0, 'table->e[i].base > cursor ||', '0 ||', 'unmap reject before writes'),
    ('unmap-fragment-kind', 0, 'plan.left = (struct appmem_extent){e->base, base, e->kind, e->flags};',
     'plan.left = (struct appmem_extent){e->base, base, APPMEM_ANON, 0};', 'unmap fragment identity'),
    ('unmap-publish-first', 3, '    rc = paging_app_unmap_free(&tx);',
     '    appmem_unmap_publish(table, &plan); rc = paging_app_unmap_free(&tx);', 'unmap extent last'),
    ('unmap-SURFACE-counter', 1, 'paging_app_unmap_reject_count++;', 'if (!ledger_surfaces[0].npages) paging_app_unmap_reject_count++;', 'unmap SURFACE diagnosed'),
    ('unmap-empty-PT-surface', 1, 'if (!app_releasable(as->owner, as->app_pt_phys[k])) goto reject;', '(void)k;', 'unmap reject before writes'),
    ('unmap-PDE-retained', 1, '        if (!pgalloc_free_n_owner(tx->as->owner, tx->as->app_pt_phys[k] / PAGE_SIZE, 1)) {',
     '        ((u32 *)P2V(tx->as->pd_phys))[APP_BAND_PDE + k] = tx->as->app_pt_phys[k] | PAGE_RW;\n        if (!pgalloc_free_n_owner(tx->as->owner, tx->as->app_pt_phys[k] / PAGE_SIZE, 1)) {', 'unmap PDE before PT free'),
    ('unmap-k0-free', 1, 'if (!k) continue;', 'if (0) continue;', 'unmap k0 retained'),

    ('map-plan-before-stage', 2, '    if (!appmem_plan_valid(table, &plan)) return APPMEM_EINVAL;',
     '    /* omitted */', 'map plan validation twice'),
    ('map-plan-before-IRQ', 2, '    if (!appmem_plan_valid(table, &plan)) {',
     '    if (0) {', 'map plan validation twice'),
    ('unmap-early-empty-NP', 1, 'if (unmap_empty(tx, k)) { pd[APP_BAND_PDE + k] = 0; continue; }',
     'if (unmap_empty(tx, k)) { u32 *pt = P2V(tx->as->app_pt_phys[k]); for (u32 j = 0; j < PTE_COUNT; j++) pt[j] &= ~PTE_PRESENT; pd[APP_BAND_PDE + k] = 0; continue; }',
     'unmap empty PT untouched before TLB'),

    ('teardown-ANON-loop', 5, 'if (e->kind == APPMEM_ANON || e->kind == APPMEM_EXEC_ARENA) {', 'if (0) {', 'teardown R5 no leftover'),
    ('teardown-entry-poison', 5, '    if (a->as->appmem_poisoned) goto poisoned;\n    shlib_addrspace_detach',
     '    shlib_addrspace_detach', 'poison quarantined'),
    ('free-range-no-break', 4, '                paging_addrspace_poison(as);\n                break;',
     '                paging_addrspace_poison(as);\n                continue;', 'free range poison stops'),
    ('poison-no-abort', 4, '    exec_addrspace_abort(as);', '    (void)as;', 'poison owner abort requested'),
    ('poison-live-count', 4, '    live_addrspaces--;\n    exec_addrspace_abort(as);',
     '    exec_addrspace_abort(as);', 'poison counted once'),
    ('resume-no-abort', 5, '    g_cur_app = a;\n    ring3_abort_check();',
     '    g_cur_app = a;', 'poison resume killed before CR3'),
    ('syscall-exit-no-abort', 5, '    ring3_abort_check();\n    exec_park_stop(frame);',
     '    exec_park_stop(frame);', 'poison syscall exit killed'),
    ('WM-clears-poison-abort', 6, 'if (!g_slot[i].as || !g_slot[i].as->appmem_poisoned)',
     'if (1)', 'poison WM clear preserves abort'),

    ('public-map-early-pointer', 7, '    0x0000,  /* mem_map */',
     '    0x0002,  /* mem_map */', 'public map address bypasses early guard'),
    ('public-unmap-early-pointer', 7, '    0x0000,  /* mem_unmap */',
     '    0x0001,  /* mem_unmap */', 'public unmap address bypasses early guard'),
    ('public-map-origin', 7, 'caller.origin != CALLER_USER) return NULL;',
     '0) return NULL;', 'public non-USER map rejected'),
    ('public-unmap-origin', 7, 'KAPI_HIT(247);\n    struct caller_access caller;\n    if (!caller_access_get(&caller) || caller.origin != CALLER_USER) return OS32_ERR_INVAL;',
     'KAPI_HIT(247);\n    struct caller_access caller;\n    if (!caller_access_get(&caller) || 0) return OS32_ERR_INVAL;', 'public non-USER unmap rejected'),
    ('public-map-kind', 7, 'flags, APPMEM_ANON, 0, &base)',
     'flags, APPMEM_EXEC_ARENA, 0, &base)', 'public map ANON'),
    ('public-unmap-translation', 7, 'return appmem_error_public(appmem_unmap(caller.as, &caller.as->appmem, (u32)base, bytes));',
     'return appmem_unmap(caller.as, &caller.as->appmem, (u32)base, bytes);', 'public unmap INVAL translated'),

]


@host32.control_session
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=['native', 'qemu'], default='native')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    host32.begin_control(args.mutate, args.runner, ROOT)
    before = {p: hashlib.sha256((ROOT / p).read_bytes()).digest() for p in TARGET_SRCS}
    bodies = [(ROOT / p).read_text() for p in SOURCES]
    cc = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-ffreestanding', '-fno-builtin',
          '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
          '-Wall', '-Wextra', '-Werror', '-Werror=implicit-function-declaration',
          '-Werror=implicit-int', '-Werror=vla', '-DPHYSMEM_HOST_TEST=1']
    try:
        with tempfile.TemporaryDirectory(prefix='os32-f4-') as d:
            tmp = pathlib.Path(d)
            includes = ['-I' + str(tmp), *['-I' + str(ROOT / p) for p in
                ('tools/tests/host_arch', '.', 'include', 'arch/x86', 'platform/pc98', 'kernel', 'lib', 'exec', 'fs', 'sdk/include/os32')]]
            cc += includes
            # Share host IF across TUs so the real allocator's short IRQ save
            # is distinguished from an incorrectly disabled transaction.
            io = (ROOT / 'tools/tests/host_arch/arch_io.h').read_text()
            (tmp / 'arch_io.h').write_text(io.replace('static unsigned int host_arch_if = 0x202U;', 'extern unsigned int host_arch_if;'))
            (tmp / 'hooks.h').write_text('#include "types.h"\n#include "io.h"\nu32 map_alloc(u32,int);\nint map_free(u32,u32,int);\nunsigned int publish_save(void);\nstruct appmem_table; struct appmem_plan;\nint host_plan_valid(const struct appmem_table *, const struct appmem_plan *);\nvoid host_pte_touch(void);\n')
            def function(source, signature):
                begin = source.index(signature)
                brace = source.index('{', begin)
                depth, end = 1, brace + 1
                while depth:
                    depth += (source[end] == '{') - (source[end] == '}')
                    end += 1
                return source[begin:end]

            def exec_parts(source):
                parts = [function(source, sig) for sig in
                         ('void exec_addrspace_abort(', 'void ring3_abort_check(', 'static void exec_teardown_app(')]
                # Execute actual safe-point tails; Linux boundary supplies the
                # non-returning kill and MMU entry. R1 separately tests real kill.
                resume = function(source, 'i32 exec_resume(')
                tail = resume[resume.index('    appslot_resume_commit('):resume.index('    return 0;   /* 到達しない */')]
                parts.append('static void host_resume_tail(int app_id) { AppSlot *a = appslot_get(app_id);\n' + tail + '\n}')
                dispatch = function(source, 'void __cdecl ring3_syscall_dispatch(')
                tail = dispatch[dispatch.index('syscall_complete:') + len('syscall_complete:'):dispatch.rfind('}')]
                parts.append('static void host_syscall_tail(u32 *frame) { int prev_caller = 0, prev_in_syscall = 0; u32 *prev_frame = 0;\n' + tail + '\n}')
                return '\n'.join(parts)

            def heap_context_parts(source):
                body = function(source, 'static void exec_restore_context(').replace('exec_restore_context', 'heap_restore_context')
                start = source.index('        if (want_ring3) paging_load_cr3(ctx->as->pd_phys);')
                end = source.index('        if (want_ring3) kselftest_run_audit', start)
                return body + '\nstatic void heap_init_context(AppSlot *ctx, int want_ring3, u32 exec_heap_base, u32 exec_heap_size) {\n' + source[start:end] + '}\n'

            def public_parts(source):
                return '\n'.join(function(source, sig) + ';' for sig in
                                 ('const u16 kapi_argptr[KAPI_FUNC_COUNT] = ',
                                  'void * __cdecl wrap_mem_map(', 'int __cdecl wrap_mem_unmap('))

            (tmp / 'public_wrap_source.c').write_text(public_parts(bodies[7]))
            (tmp / 'exec_source.c').write_text(exec_parts(bodies[5]))
            (tmp / 'heap_context_source.c').write_text(heap_context_parts(bodies[5]))
            (tmp / 'abort_clear_source.c').write_text(function(bodies[6], 'int appslot_abort_clear('))
            lease_source = (ROOT / 'exec/lease.c').read_text()
            parts = ['volatile u32 lease_revoke_fail_count, lease_revoke_all_fail_count;']
            for signature in ('static int context(', 'int lease_release(', 'int lease_revoke_sid(', 'int lease_revoke_all('):
                begin = lease_source.index(signature)
                brace = lease_source.index('{', begin)
                depth, end = 1, brace + 1
                while depth:
                    depth += (lease_source[end] == '{') - (lease_source[end] == '}')
                    end += 1
                parts.append(lease_source[begin:end])
            (tmp / 'lease_revoke_source.c').write_text('\n'.join(parts))

            (tmp / 'heap_source.c').write_text(bodies[8])
            (tmp / 'heap_wrap_source.c').write_text('\n'.join(function(bodies[7], sig) for sig in ('void * __cdecl wrap_mem_alloc(', 'void __cdecl wrap_mem_free(')))
            for src in ('paging', 'pgalloc'):
                (tmp / (src + '_source.c')).write_text((ROOT / ('kernel/' + src + '.c')).read_text())

            def compile_source(source, key, index):
                src, obj = tmp / (key + '.c'), tmp / (key + '.o')
                if index >= 4:
                    if index == 4: (tmp / 'paging_source.c').write_text(source)
                    if index == 5:
                        (tmp / 'exec_source.c').write_text(exec_parts(source))
                        (tmp / 'heap_context_source.c').write_text(heap_context_parts(source))
                    if index == 7:
                        (tmp / 'public_wrap_source.c').write_text(public_parts(source))
                        (tmp / 'heap_wrap_source.c').write_text('\n'.join(function(source, sig) for sig in ('void * __cdecl wrap_mem_alloc(', 'void __cdecl wrap_mem_free(')))
                    if index == 8: (tmp / 'heap_source.c').write_text(source)
                    if index == 6: (tmp / 'abort_clear_source.c').write_text(function(source, 'int appslot_abort_clear('))
                    host32.build(cc + ['-c', str(ROOT / 'tools/tests/appmem_map_host.c'), '-o', str(obj)],
                                 check=True, capture_output=True, text=True, timeout=30)
                    (tmp / 'public_wrap_source.c').write_text(public_parts(bodies[7]))
                    (tmp / 'heap_wrap_source.c').write_text('\n'.join(function(bodies[7], sig) for sig in ('void * __cdecl wrap_mem_alloc(', 'void __cdecl wrap_mem_free(')))
                    # Restore inputs before the next (sequential) mutation.
                    (tmp / 'heap_source.c').write_text(bodies[8])
                    (tmp / 'heap_context_source.c').write_text(heap_context_parts(bodies[5]))
                    (tmp / 'paging_source.c').write_text(bodies[4])
                    (tmp / 'exec_source.c').write_text(exec_parts(bodies[5]))
                    (tmp / 'abort_clear_source.c').write_text(function(bodies[6], 'int appslot_abort_clear('))
                    return obj
                if index in (2, 3): source = source.replace('irq_save()', 'publish_save()')
                if index == 2: source = source.replace('appmem_plan_valid(table, &plan)', 'host_plan_valid(table, &plan)')
                src.write_text(source)
                extra = ['-include', str(tmp / 'hooks.h')]
                if index == 1:
                    source = source.replace('*app_entry(tx, va) |= PTE_PRESENT;', '{ host_pte_touch(); *app_entry(tx, va) |= PTE_PRESENT; }').replace('*unmap_entry(tx, va) &= ~PTE_PRESENT;', '{ host_pte_touch(); *unmap_entry(tx, va) &= ~PTE_PRESENT; }')
                    src.write_text(source)
                    extra += ['-Dpgalloc_alloc_phys=map_alloc', '-Dpgalloc_free_n_owner=map_free']
                host32.build(cc + extra + ['-c', str(src), '-o', str(obj)], check=True,
                               capture_output=True, text=True, timeout=30)
                return obj

            objects = []
            for key, path in [('fixture', 'tools/tests/appmem_map_host.c'), ('physmem', 'kernel/physmem.c')]:
                obj = tmp / (key + '.o')
                host32.build(cc + ['-c', str(ROOT / path), '-o', str(obj)], check=True,
                               capture_output=True, text=True, timeout=30)
                objects.append(obj)
            normal = [compile_source(body, 'normal' + str(i), i) for i, body in enumerate(bodies[:4])]

            def run(objs, key):
                exe = tmp / (key + '.elf')
                host32.build(['gcc', '-m32', '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                                *map(str, (objects if len(objs) == 4 else objects[1:]) + objs), '-o', str(exe)], check=True,
                               capture_output=True, text=True, timeout=30)
                return host32.run([str(exe)], runner=args.runner, capture_output=True, text=True, timeout=host32.RUN_TIMEOUT)

            with host32.control(args.mutate, args.runner, ROOT) as run_control:
                if run_control:
                    r = run(normal, 'normal')
                    print(r.stdout + r.stderr, end='')
                    assert r.returncode == 0, (r.returncode, r.stdout, r.stderr)
                    print('PASS runner=' + args.runner)
            if args.mutate:
                def one(entry):
                    index, (name, target, old, new, expected) = entry
                    start = time.monotonic()
                    try:
                        assert bodies[target].count(old) == 1, (name, bodies[target].count(old))
                        obj = compile_source(bodies[target].replace(old, new), f'mut{index}', target)
                        objs = normal.copy()
                        if target < 4: objs[target] = obj
                        else: objs = [obj] + objs
                        r = run(objs, f'mut{index}')
                    except subprocess.CalledProcessError as e:
                        return 'COMPILE_LINK_ERROR', name, time.monotonic() - start, (e.stdout or '') + (e.stderr or '')
                    except subprocess.TimeoutExpired as e:
                        return 'TIMEOUT', name, time.monotonic() - start, str(e)
                    except AssertionError as e:
                        return 'ERROR', name, time.monotonic() - start, str(e)
                    status = ('RED' if r.returncode == 1 and f'FAIL: {expected}\n' in r.stdout else
                              'SURVIVED' if r.returncode == 0 else 'SIGNAL' if r.returncode < 0 else 'ERROR')
                    return status, name, time.monotonic() - start, r.stdout + r.stderr
                # Fixture-copy mutants share include paths, so run those sequentially.
                results = list(run_ordered(one, [(i, m) for i, m in enumerate(MUTANTS) if m[1] < 4]))
                results += [one((i, m)) for i, m in enumerate(MUTANTS) if m[1] >= 4]
                for status, name, seconds, output in results:
                    print(f'{status} (runtime): {name} ({seconds:.2f}s)')
                    if status != 'RED': print(output)
                times = [r[2] for r in results]
                counts = {s: sum(r[0] == s for r in results) for s in
                          ('RED', 'SURVIVED', 'COMPILE_LINK_ERROR', 'TIMEOUT', 'SIGNAL', 'ERROR')}
                print(f'MUTATIONS {counts}; median={statistics.median(times):.2f}s max={max(times):.2f}s')
                assert counts['RED'] == len(MUTANTS), counts
    finally:
        assert all(hashlib.sha256((ROOT / p).read_bytes()).digest() == digest for p, digest in before.items()), 'input changed during test'


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as e:
        print(e.stdout or '', e.stderr or '')
        raise
