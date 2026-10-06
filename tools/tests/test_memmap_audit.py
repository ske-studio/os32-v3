"""e10c real master/AS/lease/gfx audit; hardware-only edges use the e4 fixture."""
import argparse
from pathlib import Path
import tempfile
import host32
from test_gfx_kernel_fb import run_case, ROOT, TARGET_SRCS
import re

def function(text, name):
    m = re.search(r"^[^\n;]*\b" + name + r"\([^;{]*\)\s*\{", text, re.M)
    assert m, name
    end = text.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[m.start():end]

from mutpar import run_ordered

MUTANTS = [
 ('lease-scan-duplicate', 'exec/lease.c', '    for (u32 di = 0; di < MEM_LEASE_MAX_PDES; di++) {', '    for (u32 repeat = 0; repeat < 2; repeat++)\n    for (u32 di = 0; di < MEM_LEASE_MAX_PDES; di++) {', 'FAIL scan_max[0] == PTE_COUNT'),
 ('AS-IRQ-unbounded', 'exec/lease.c', '        irq_restore(flags);\n    }\n    /* Alias/descriptor', '        (void)flags;\n    }\n    /* Alias/descriptor', 'FAIL scan_max[0] == PTE_COUNT'),
 ('v86-client-revoke', 'gfx/gfx_core.c', '    gfx_reinit_surface_roles(0, 1);', '    gfx_reinit_surface_roles(0, 0);', 'FAIL client->gen == cref.generation && client->lease_count == 1'),
 ('tvram-unready', 'exec/system_surface.c', '!(gfx_surface_unready & (1U << (sf - ledger_surfaces)))', '1', 'FAIL !system_surface_source(LEDGER_ROLE_TVRAM, &tvsrc) && !tvsrc.ready'),
 ('aperture-user', 'kernel/paging.c', '    if ((entry & PTE_USER) &&\n        !ledger_surface_find(LEDGER_SF_CIRRUS, LEDGER_ROLE_CLIENT)) return 0;', '', 'FAIL paging_master_audit(exec_tramp_page_addr()) > 0'),
 ('failed-map-published', 'gfx/gfx_core.c', '            ledger_resources[rid].map_first = ledger_resources[rid].map_end = 0;', '', 'FAIL !paging_master_audit(exec_tramp_page_addr())'),
 ('lifetime-boot-count', 'kernel/kselftest.c', '    audit_runs++;', '    audit_runs++;\n    ksel_pass++;', 'FAIL ksel_pass == boot_pass && ksel_fail == boot_fail'),
 ('selected-check', 'gfx/gfx_core.c', 'int gfx_selected_selfcheck(void)\n{\n    struct surface_query_source client, display;\n    if (gfx_selected_source(LEDGER_ROLE_CLIENT, &client) ||\n        gfx_selected_source(LEDGER_ROLE_DISPLAY, &display) ||\n        !client.ready || !display.ready || client.count != 1) return 0;\n    for (u32 i = 0; i < display.count; i++)\n        if (!ledger_surface_validate(&ledger_surfaces[display.refs[i].sid])) return 0;\n    const struct ledger_surface *sf = &ledger_surfaces[client.refs[0].sid];\n    if (!ledger_surface_validate(sf) || !g_backend ||\n        g_backend->bb_base != bb[0] || g_backend->bb_size != sf->npages * PAGE_SIZE ||\n        g_backend->bb_pitch != sf->pitch || g_backend->bb_format != sf->format) return 0;\n    u8 *base = sf->backing >= LEDGER_SB_VRAM ?\n               (u8 *)P2V_IO(sf->first * PAGE_SIZE) : (u8 *)P2V(sf->first * PAGE_SIZE);\n    for (u32 i = 0; i < 4; i++)\n        if (bb[i] != (i < sf->planes ? base + sf->plane_offset[i] : 0)) return 0;\n    return bb_b == bb[0] && bb_r == bb[1] && bb_g == bb[2] && bb_i == bb[3];\n}', 'int gfx_selected_selfcheck(void)\n{\n    return 1;\n}', 'FAIL !gfx_selected_selfcheck()'),

 ('shlib-owner-only', 'kernel/paging.c', 'shared_ro && shared_ro(va, e & ~(u32)(PAGE_SIZE - 1))', '(shared_ro != 0 && pgalloc_page_owned(e >> PAGE_SHIFT, LEDGER_OWNER_SHLIB))', 'FAIL lease_audit_all() > 0'),
 ('revoke-all-count', 'exec/lease.c', '    lease_revoke_all_fail_count += (u32)failed;', '', 'FAIL lease_revoke_all(0) == 1 && lease_revoke_all_fail_count == failed + 1'),
 ('master-PCD', 'kernel/paging.c', 'PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT)) !=\n                (a | want)', 'PAGE_RW | PTE_USER | PTE_PWT)) !=\n                (a | (want & ~PTE_PCD))', 'FAIL paging_master_audit(exec_tramp_page_addr()) > 0'),
 ('AS-PDE0', 'kernel/paging.c', 'if ((d & ~(PTE_ACCESSED | PTE_DIRTY)) !=\n                (page_directory[di]', 'if (di != 0 && (d & ~(PTE_ACCESSED | PTE_DIRTY)) !=\n                (page_directory[di]', 'FAIL lease_audit_all() > 0'),
 ('TVRAM-revoke', 'gfx/gfx_core.c', '        (void)lease_revoke_surface(sid);\n        if (ledger_surface_regen(sid, 0))', '        if (ledger_surface_regen(sid, 0))', 'FAIL tv->gen == ref.generation + 1 && !tv->lease_count'),
 ('DISPLAY-revoke', 'gfx/gfx_core.c', '    gfx_reinit_surface_roles(0, 1);\n    gfx_reinit_surface_roles(1, 1);\n    gfx_reinit_tvram();', '    gfx_reinit_tvram();', 'FAIL tv->gen == ref.generation + 1 && !tv->lease_count && display->gen == gen + 1'),
 ('alias-cache', 'exec/lease.c', 'if (sf->npages && (!ledger_surface_validate(sf) || !alias_cache(sf))) bad++;', 'if (sf->npages && (!ledger_surface_validate(sf))) bad++;', 'FAIL lease_audit_all() > 0'),
]

def stage(tmp, sources, changes=()):
    text = (ROOT/'kernel/kselftest.c').read_text()
    for path, old, new in changes:
        if path == 'kernel/kselftest.c':
            assert text.count(old) == 1
            text = text.replace(old, new)
    text = text[text.index('/* Lifecycle diagnostics,'):]
    (tmp/'audit_slice.inc').write_text(text)
    # Count real scan iterations and the largest interval with IF clear.
    arch = tmp/'arch_io.h'
    s = arch.read_text().replace('static inline void irq_restore(unsigned int flags) { host_arch_if = flags; }',
        'extern void host_irq_end(unsigned int);\nstatic inline void irq_restore(unsigned int flags) { host_irq_end(flags); host_arch_if = flags; }')
    arch.write_text(s)
    for filename, name, old in (
        ('lease.c', 'lease_check', '            pte = pt[ti];'),
        ('paging_host_source.c', 'paging_as_audit', '                u32 e = pt[ti],'),
        ('paging_host_source.c', 'paging_master_audit', '            u32 p = di * PTE_COUNT'),
        ('lease.c', 'alias_cache', '        u32 pa = p * PAGE_SIZE'),
        ('pgalloc_host_source.c', 'ledger_selfcheck', '        o = owner_map[p];')):
        path = tmp/filename
        s = path.read_text()
        body = function(s, name)
        assert old in body, (name, old)
        kind = 1 if name == 'ledger_selfcheck' else 0
        instrumented = body.replace(old, f'        host_scan({kind});\n'+old)
        path.write_text('extern void host_scan(int);\n' + s.replace(body, instrumented))
    path = tmp/'paging_host_source.c'
    s = path.read_text()
    body = function(s, 'paging_map_phys')
    s = s.replace(body, body.replace('    u32 v =', '    if (host_map_fail && phys_addr == PEGC_LINEAR_BASE) return -1;\n    u32 v ='))
    path.write_text('#include "pegc.h"\nextern int host_map_fail;\n'+s)
    path = tmp/'system_surface.c'
    text = (ROOT/'exec/system_surface.c').read_text()
    for unit, old, new in changes:
        if unit == 'exec/system_surface.c':
            assert text.count(old) == 1
            text = text.replace(old, new)
    path.write_text('#define __KERNEL_BUILD__ 1\n'+text)
    sources.append(path)

@host32.control_session
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner', choices=['native','qemu'], default='native')
    p.add_argument('--mutate', action='store_true')
    a=p.parse_args()
    host32.begin_control(a.mutate, a.runner, ROOT)
    with tempfile.TemporaryDirectory(prefix='e10c-audit-') as d:
        def run(changes=(), failure=False):
            return run_case(None,a.runner,changes,('#define TEST_MAP_FAILURE\n' if failure else '')+'#include "memmap_audit_host.c"\n',object_cache=Path(d),stage=lambda tmp, sources: stage(tmp, sources, changes))
        with host32.control(a.mutate,a.runner,ROOT) as normal:
            if normal:
                for failure in (False, True):
                    r=run(failure=failure); print(r.stdout+r.stderr,end=''); assert r.returncode == 0, r.returncode
        if a.mutate:
            for name,path,old,new,expected in MUTANTS:
                if expected is None: continue
                r=run([(path,old,new)], failure=name == "failed-map-published")
                assert r.returncode == 1 and expected in r.stdout, (name,r.returncode,r.stdout,r.stderr)
                print('RED',name)
if __name__ == '__main__': main()
