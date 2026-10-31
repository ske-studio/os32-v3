"""T2e e3: PC98 DISPLAY bundle and native alias lifetime, real ILP32 paths."""
CHECK = dict(name='surface-bundle-host', tier=1,
    covers=['include/platform_emergency.h', 'kernel/v86/v86_module.h', 'userland/lib/rt/ls.c', 'sdk/include/os32/ls.h', 'exec/access_walk.c', 'exec/exec.c', 'exec/lease.c', 'exec/redir_access.c', 'exec/surface_query.c', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'mm/paging.c', 'platform/pc98/platform_memory_desc.h', 'include/platform_memory.h', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'kernel/v86/v86_mem.c', 'tools/tests/access_walk_host.c', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/surface_bundle_host.c', 'tools/tests/test_access_walk.py', 'tools/tests/kernel_layout.py', 'tools/tests/test_surface_lease.py', 'tools/tests/surface_lease_host.c', 'exec/ring3_ls.c', 'kapi/kapi_generated.c', 'mm/shm.c', 'sdk/kapi.json', 'tools/tests/public_surface_host.c', 'tools/tests/test_public_surface.py', 'README.md', 'docs/INDEX.md', 'docs/KAPI_SPEC.md', 'exec/ring3_ls.S', 'fs/ext2/ext2_vfs.c', 'fs/vfs.c', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/kapi_generated.rs', 'tools/check_kapi_version.py', 'tools/tests/kcallback_host.c', 'tools/tests/kcallback_result_host.c', 'tools/tests/test_kcallback.py', 'userland/rust/libos32gui/src/cfgro.rs', 'userland/tests/kcallback_test.c', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py'],
    needs=['make:all'], budget=2.632578)

import subprocess
from test_surface_lease import main
TARGET_SRC = ['exec/lease.c', 'exec/surface_query.c', 'exec/exec.c',
              'kernel/v86/v86_mem.c', 'mm/paging.c', 'mm/pgalloc.c',
              'tools/tests/surface_bundle_host.c']
MUTANTS = [
    ('lease', 'source->backend != gfx_sf_backend()', '0', 'active backend mismatch'),
    ('lease', '!check_caller_write_range(&caller, user_out, sizeof(result))', '0', 'bundle output preflight'),
    ('lease', 'kctx_irq_depth || kctx_exc_depth || !source ||', '!source ||', 'bundle entry context'),
    ('surface_query', 'refs[i].generation != snap.desc[i].ref.generation', '0',
     'bundle generation checks removed', [
         ('lease', 'sf->gen != refs[i].generation', '0'),
         ('paging', 'ledger_surfaces[m->sid].gen != m->generation', '0')]),
    ('lease', 'if (!copy_to_caller(&caller, user_out, &result, sizeof(result)))',
     'if (!copy_to_caller(&caller, user_out, &result, 4) ||\n        !copy_to_caller(&caller, user_out, &result, sizeof(result)))', 'bundle partial output'),
    ('lease', 'count != SURFACE_QUERY_MAX)', 'count > SURFACE_QUERY_MAX)', 'bundle count'),
    ('lease', 'source->backend != LEDGER_SF_PC98 ||', '0 ||', 'selected backend'),
    ('lease', 'source->role != LEDGER_ROLE_DISPLAY ||', '0 ||', 'bundle role'),
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
             extra_sources={'v86': 'kernel/v86/v86_mem.c'},
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
