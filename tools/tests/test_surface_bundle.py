"""T2e e3: PC98 DISPLAY bundle and native alias lifetime, real ILP32 paths."""
CHECK = dict(name='surface-bundle-host', tier=1,
    covers=['sys/platform/pc98/display_bundle.c', 'sys/platform/powermac/display_bundle.c', 'sys/include/display_provider.h', 'sys/arch/x86/kernel/kapi_call.c', 'sys/include/kapi_call.h', 'sys/include/syscall_code.h', 'sys/arch/x86/kernel/syscall_code.c', 'sys/platform/pc98/legacy/lowmap.c', 'sys/kern/mm/paging_lowmap.h', 'sys/kern/mm/paging_lowmap.inc', 'sys/include/platform_emergency.h', 'sys/platform/pc98/legacy/v86_module.h', 'userland/lib/rt/ls.c', 'sdk/include/os32/ls.h', 'sys/kern/exec/access_walk.c', 'sys/kern/exec/exec.c', 'sys/kern/exec/lease.c', 'sys/kern/exec/redir_access.c', 'sys/kern/exec/surface_query.c', 'sys/platform/pc98/kdebug/kselftest.c', 'kdebug/kselftest/**', 'sys/kern/mm/paging.c', 'sys/platform/pc98/platform_memory_desc.h', 'sys/include/platform_memory.h', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shlib.c', 'sys/platform/pc98/legacy/v86_mem.c', 'tools/tests/access_walk_host.c', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/surface_bundle_host.c', 'tools/tests/test_access_walk.py', 'tools/tests/kernel_layout.py', 'tools/tests/test_surface_lease.py', 'tools/tests/surface_lease_host.c', 'sys/kern/exec/ring3_ls.c', 'sys/kapi/kapi_generated.c', 'sys/kern/mm/shm.c', 'sdk/kapi.json', 'tools/tests/public_surface_host.c', 'tools/tests/test_public_surface.py', 'README.md', 'docs/INDEX.md', 'docs/KAPI_SPEC.md', 'sys/arch/x86/kernel/ring3_ls.S', 'sys/fs/ext2/ext2_vfs.c', 'sys/fs/vfs.c', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/kapi_generated.rs', 'tools/check_kapi_version.py', 'tools/tests/kcallback_host.c', 'tools/tests/kcallback_result_host.c', 'tools/tests/test_kcallback.py', 'userland/rust/libos32gui/src/cfgro.rs', 'userland/diag/kcallback_test.c', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=4.7,
    basis=['KAPI_SPEC §3-3', 'docs/decisions/display/SURFACE_LEASE_BUNDLE_DESIGN.md §5'],
)

import subprocess
from test_surface_lease import main
TARGET_SRC = ['sys/platform/pc98/display_bundle.c', 'sys/platform/powermac/display_bundle.c', 'sys/kern/exec/lease.c', 'sys/kern/exec/surface_query.c', 'sys/kern/exec/exec.c',
              'sys/platform/pc98/legacy/v86_mem.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c',
              'tools/tests/surface_bundle_host.c', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h']
MUTANTS = [
    ('lease', 'source->backend != exec_surface_backend()', '0', 'active backend mismatch'),
    ('lease', '!check_caller_write_range(&caller, user_out, sizeof(result))', '0', 'bundle output preflight'),
    ('lease', 'kctx_irq_depth || kctx_exc_depth || !source)', '!source)', 'bundle entry context'),
    ('surface_query', 'refs[i].generation != snap.desc[i].ref.generation', '0',
     'bundle generation checks removed', [
         ('lease', 'sf->gen != refs[i].generation', '0'),
         ('paging', 'ledger_surfaces[m->sid].gen != m->generation', '0')]),
    ('lease', 'if (!copy_to_caller(&caller, user_out, &result, sizeof(result)))',
     'if (!copy_to_caller(&caller, user_out, &result, 4) ||\n        !copy_to_caller(&caller, user_out, &result, sizeof(result)))', 'bundle partial output'),
    ('lease', 'count != source->count', '0', 'bundle count'),
    ('lease', 'count * sizeof(refs[0])', 'sizeof(refs)', 'bundle fixed input length'),
    ('surface_query', '!display_provider_bundle_ok(role, source.backend)', '0', 'provider gate'),
    ('pc98_bundle', 'role == LEDGER_ROLE_DISPLAY && backend == LEDGER_SF_PC98',
     '((void)role, backend == LEDGER_SF_PC98)', 'PC98 provider role'),
    ('pc98_bundle', 'role == LEDGER_ROLE_DISPLAY && backend == LEDGER_SF_PC98',
     '((void)backend, role == LEDGER_ROLE_DISPLAY)', 'PC98 provider backend'),
    ('powermac_bundle', 'role == LEDGER_ROLE_CLIENT && backend == LEDGER_SF_OF_FB',
     '((void)role, backend == LEDGER_SF_OF_FB)', 'PowerMac provider role'),
    ('powermac_bundle', 'role == LEDGER_ROLE_CLIENT && backend == LEDGER_SF_OF_FB',
     '((void)backend, role == LEDGER_ROLE_CLIENT)', 'PowerMac provider backend'),
    ('lease', 'if (source->role == LEDGER_ROLE_DISPLAY)\n        exec_surface_bridge_begin(access);\n    saved = irq_save();',
     'exec_surface_bridge_begin(access);\n    saved = irq_save();', 'CLIENT must not enter DISPLAY bridge'),
    ('lease', 'if (source->role == LEDGER_ROLE_DISPLAY)\n        exec_surface_bridge_begin(access);\n    saved = irq_save();',
     'saved = irq_save();', 'PC98 DISPLAY bridge omitted'),
    ('lease', '!ledger_surface_validate(sf) || !alias_cache(sf)) return LEASE_INVAL;',
     '!ledger_surface_validate(sf) || !alias_cache(sf)) {\n'
     '            if (count == 4 && i == 3) lease_next_token++;\n'
     '            return LEASE_INVAL;\n'
     '        }', 'fourth-plane rejection consumes a token'),
    ('lease', 'for (i = 0; i < count; i++) {\n            if (lease_release',
     'for (i = 1; i < count; i++) {\n            if (lease_release', 'rollback skips first token'),
    ('lease', 'if (!rc) rc = surface_query_refs(source, refs, count, access);',
     '/* whole-bundle validation omitted */', 'INVAL before STALE'),
    ('lease', 'irq_restore(saved);\n    if (rc) return rc;\n    result.count', 'irq_restore(saved | 0x200);\n    if (rc) return rc;\n    result.count', 'bundle IF'),
    ('lease', 'result.count = count;', 'result.count = 1;', 'four outputs'),
    ('lease', 'if (lease_release(caller.as, result.views[i].token))', 'if (1)', 'bundle rollback'),
    ('lease', 'lease_next_token = next_token;', '(void)next_token;', 'bundle rollback tokens'),
    ('lease', 'kmemcpy(caller.as->leases, previous, sizeof(previous));',
     '(void)previous;', 'bundle rollback slot bytes'),
    ('lease', '                lease_rollback_fail_count++;',
     '                (void)0;', 'bundle rollback failure count'),
    ('lease', 'if (!failed) {', 'if (1) {', 'failed release must retain slots'),
    ('paging', 'native_cache_bits(phys)', '0', 'initial native UC'),
    ('lease', '(sf->cache == LEDGER_CACHE_UC ? PTE_PCD : 0);', '0;', 'lease loses UC'),
    ('v86', 'e->setup_flags) != 0', '(e->setup_flags & ~PTE_PCD)) != 0', 'V86 setup loses UC'),
    ('v86', 'e->teardown_flags & PTE_PCD', '(e->teardown_flags & ~PTE_PCD) & PTE_PCD', 'V86 teardown table loses UC'),
    ('v86', '(e->setup_flags & PTE_PCD) != want || pcd != want', '((void)want, 0)', 'V86 ledger cache mismatch'),
    ('v86', 'v86_restore_mismatch += paging_v86_restore(&v86_session);', 'v86_session.low_pte[V86_GVRAM_E_START / PAGE_SIZE] &= ~PTE_PCD; v86_restore_mismatch += paging_v86_restore(&v86_session);', 'V86 teardown loses UC'),
    ('surface_query', 'if (!gfx || (gui && appslot_gfx_owner() != c.app_id))',
     'if (!gfx)', 'ordinary GUI DISPLAY'),
]
if __name__ == '__main__':
    try:
        main(fixture_name='surface_bundle_host.c', mutants=MUTANTS,
             extra_sources={'v86': 'sys/platform/pc98/legacy/v86_mem.c',
                            'pc98_bundle': 'sys/platform/pc98/display_bundle.c',
                            'powermac_bundle': 'sys/platform/powermac/display_bundle.c'},
             lease_hook='#define paging_lease_unmap host_bundle_unmap\n')
    except subprocess.CalledProcessError as error:
        print(error.stdout or '', error.stderr or '')
        raise


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_surface_bundle.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
