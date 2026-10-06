"""T2e e3: PC98 DISPLAY bundle and native alias lifetime, real ILP32 paths."""
import subprocess
from test_surface_lease import main
TARGET_SRC = ['exec/lease.c', 'exec/surface_query.c', 'exec/exec.c',
              'kernel/v86_mem.c', 'kernel/paging.c', 'kernel/pgalloc.c',
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
    ('paging', '(PC98_NATIVE_VRAM(phys) ? PTE_PCD : 0)', '0', 'initial native UC'),
    ('exec', 'paging_addrspace_map_user_keep(ctx->as,\n            TVRAM_CHAR_BASE',
     'paging_addrspace_map_user_range(ctx->as,\n            TVRAM_CHAR_BASE', 'exec loses UC'),
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
             extra_sources={'v86': 'kernel/v86_mem.c', 'exec': 'exec/exec.c'},
             lease_hook='#define paging_lease_unmap host_bundle_unmap\n')
    except subprocess.CalledProcessError as error:
        print(error.stdout or '', error.stderr or '')
        raise
