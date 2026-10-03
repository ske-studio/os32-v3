"""T2e e5: real gfx/three backends/ledger/lease/query with three ASes (ILP32)."""
import argparse
import hashlib
import pathlib
import tempfile
import time
from mutpar import run_ordered
from test_gfx_kernel_fb import run_case, TARGET_SRCS, ROOT

HOOKS = [
 ('gfx/gfx_core.c', '    gfx_bind_client(); /* bind before init_200; retain 400-line plane stride */',
  '    { extern void host_reinit_event(int); host_reinit_event(1); }\n    gfx_bind_client(); /* bind before init_200; retain 400-line plane stride */'),
 ('kernel/pgalloc.c', '    next.gen++;',
  '    { extern void host_reinit_event(int); host_reinit_event(2); }\n    next.gen++;'),
]

MUTANTS = [
 ('gen-before-revoke', [('gfx/gfx_core.c', '            gfx_reinit_pending |= bit;',
   '            sf->gen++; gfx_reinit_pending |= bit;')], 'FAIL ledger_surfaces[i].gen == expected_gen[i]'),
 ('revoke-after-bind', [
   ('gfx/gfx_core.c', '            (void)lease_revoke_surface(i);', ''),
   ('gfx/gfx_core.c', '            if (ledger_surface_regen(i, 0)) gfx_surface_unready &= ~bit;',
    '            (void)lease_revoke_surface(i);\n            if (ledger_surface_regen(i, 0)) gfx_surface_unready &= ~bit;')], 'FAIL !ledger_surfaces[i].lease_count'),
 ('no-gen', [('kernel/pgalloc.c', '    next.gen++;', '')],
  'FAIL pc->gen == client.refs[0].generation+1 && !pc->lease_count'),
 ('third-AS', [('exec/lease.c', 'failed += lease_revoke_sid(a->as, sid);',
   'failed += lease_revoke_sid(a->as, sid < LEDGER_MAX_SURFACES ? LEDGER_MAX_SURFACES : sid);')], 'FAIL pt[j] == image[n]'),
 ('caller-reload', [('gfx/gfx_core.c',
   '    /* Reload the caller even when it owns leases revoked under master. */\n    paging_load_cr3(cr3);',
   '    (void)cr3;')], 'FAIL host_cr3 == caller_root && host_arch_if == caller_flags'),
 ('resurrect-on-failure', [('exec/lease.c',
   '    int failed = 0;\n    for (int id = APP_ID_SHELL;',
   '    u32 old = appslot_at(APP_ID_MIN)->as->leases[0].token;\n    int failed = 0;\n    for (int id = APP_ID_SHELL;'),
   ('exec/lease.c', '    return failed;\n}\n\nint lease_revoke_all',
   '    if (failed) appslot_at(APP_ID_MIN)->as->leases[0].token = old;\n    return failed;\n}\n\nint lease_revoke_all')], 'FAIL !space.leases[0].token'),
 ('bundle-gen', [('exec/surface_query.c', 'refs[i].generation != snap.desc[i].ref.generation', '0')],
  'FAIL surface_query_refs(&fresh,display.refs,4,LEDGER_PERM_RW) == OS32_ERR_STALE'),
 ('200-geometry', [('gfx/gfx_core.c', '            if (ledger_surface_regen(i, 0)) gfx_surface_unready &= ~bit;',
   '            struct ledger_surface geom = *sf;\n            if (sf->backend == LEDGER_SF_PC98) { geom.height = gfx_current_height; for (u32 j=0;j<sf->planes;j++) geom.plane_offset[j] = geom.pitch * geom.height * j; }\n            if (ledger_surface_regen(i, &geom)) gfx_surface_unready &= ~bit;')], 'FAIL pc->height == GFX_HEIGHT && pc->first == phys && pc->npages == pages'),
 ('200-clear-range', [('gfx/gfx_core.c', '    _gfx_common_init(GFX_PLANE_SZ_200);',
   '    _gfx_common_init(GFX_PLANE_SZ_200 / 2);')], 'FAIL pixels[j] == 0'),
 ('revoke-stop-first', [('exec/lease.c', '            failed++;\n            lease_revoke_fail_count++;',
   '            failed++;\n            lease_revoke_fail_count++;\n            return failed;')],
  'FAIL other.leases[0].token == bv.token && !other.leases[1].token'),
 ('publisher-open', [('gfx/gfx_core.c', '    unsigned int flags = irq_save();\n    u32 cr3 = paging_current_cr3(), i;',
   '    gfx_started = 1;\n    unsigned int flags = irq_save();\n    u32 cr3 = paging_current_cr3(), i;')],
  'FAIL gfx_surface_source(LEDGER_ROLE_CLIENT,&source) == OS32_ERR_INVAL && !source.ready'),
]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner',choices=['native','qemu'],default='native')
    p.add_argument('--mutate',action='store_true')
    a=p.parse_args()
    texts={path:(ROOT/path).read_text() for path in TARGET_SRCS}
    before={path:hashlib.sha256(text.encode()).digest() for path,text in texts.items()}
    with tempfile.TemporaryDirectory(prefix='os32-e5-objects-') as directory:
        def run(changes):
            return run_case(None,a.runner,HOOKS+changes,'#include "gfx_reinit_host.c"\n',
                            texts,pathlib.Path(directory))
        r=run([])
        print(r.stdout+r.stderr,end='')
        assert r.returncode == 0,r.returncode
        if a.mutate:
            def one(m):
                start=time.monotonic()
                result=run(m[1])
                assert result.returncode == 1 and m[2] in result.stdout, (m[0],result.returncode,result.stdout,result.stderr)
                return f'RED {m[0]} {time.monotonic()-start:.2f}s'
            for message in run_ordered(one,MUTANTS): print(message)
            print(f'PASS {len(MUTANTS)}/{len(MUTANTS)} runtime mutants')
    assert all(hashlib.sha256((ROOT/path).read_bytes()).digest() == digest
               for path,digest in before.items())
if __name__ == '__main__': main()
