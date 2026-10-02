"""T2b: actual paging/ledger/lease ILP32 runtime and compiled runtime mutants."""
import host32
import pathlib
import subprocess
import tempfile
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-ffreestanding', '-fno-pie',
         '-fno-stack-protector', '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra', '-Werror']
SOURCES = ('kernel/paging.c', 'kernel/pgalloc.c', 'exec/lease.c')
MUTANTS = [
    ('surface registration master guard removed', 1,
     'paging_current_cr3() != paging_kernel_pd_phys() ||\n        kctx_irq_depth', 'kctx_irq_depth'),
    ('free before active TLB synchronization', 0,
     'if (paging_current_cr3() == as->pd_phys) paging_load_cr3(as->pd_phys);\n    for (k = 1; k < MEM_LEASE_MAX_PDES; k++)',
     '/* synchronization omitted */\n    for (k = 1; k < MEM_LEASE_MAX_PDES; k++)'),
    ('backing free before active TLB synchronization', 2,
     '        if (paging_lease_unmap(as, l->base, l->npages)) {',
     '        if (paging_lease_unmap(as, l->base, l->npages)) {'),
    ('shared PT write', 0,
     '((u32 *)P2V(phys))[(va >> PAGE_SHIFT) % PTE_COUNT] =\n            (maps[i].phys',
     'page_tables[0][(va >> PAGE_SHIFT) % PTE_COUNT] =\n            (maps[i].phys'),
    ('PCD lost', 2, '(sf->cache == LEDGER_CACHE_UC ? PTE_PCD : 0)', '0'),
    ('rollback leaked PT', 0, 'pgalloc_free_n_owner(as->owner, pending[k] / PAGE_SIZE, 1);', '(void)pending[k];'),
    ('generation ignored', 2, 'sf->gen != refs[i].generation', '0'),
    ('plane end unchecked', 1, 'plane_bytes > bytes - sf->plane_offset[i]', 'plane_bytes > bytes'),
    ('permission ignored', 2, '(access == LEDGER_PERM_RW && sf->perm_max != LEDGER_PERM_RW)', '0'),
    ('free before PDE invalidation', 0,
     'if (j == PTE_COUNT) pd[(MEM_LEASE_BASE >> 22) + k] = 0;',
     'if (j == PTE_COUNT) { pgalloc_free_n_owner(as->owner, as->lease_pt_phys[k] / PAGE_SIZE, 1); pd[(MEM_LEASE_BASE >> 22) + k] = 0; }'),
]

def replace_once(body, old, new):
    assert body.count(old) == 1, (old, body.count(old))
    return body.replace(old, new)


def run(changes=None):
    with tempfile.TemporaryDirectory(prefix='os32-lease-') as tmpdir:
        tmp = pathlib.Path(tmpdir)
        texts = [(ROOT / p).read_text() for p in SOURCES]
        if changes:
            name, index, old, new = changes
            assert texts[index].count(old) == 1, name
            texts[index] = texts[index].replace(old, new)
            if name == 'backing free before active TLB synchronization':
                # Reorder last-reference backing return ahead of PTE/PT teardown.
                old_block = ('        l->token = 0;\n        sf->lease_count--;\n'
                             '        if (sf->closing && !sf->lease_count) (void)ledger_surface_release(l->sid);\n')
                assert texts[2].count(old_block) == 1
                texts[2] = texts[2].replace(old_block, '')
                marker = '        if (paging_lease_unmap(as, l->base, l->npages)) {'
                assert texts[2].count(marker) == 1
                texts[2] = texts[2].replace(marker, old_block + marker)
            if name == 'permission ignored':
                texts[0] = replace_once(texts[0], '((m->flags & PTE_RW) && ledger_surfaces[m->sid].perm_max != LEDGER_PERM_RW)', '0')
            if name == 'PCD lost':
                texts[0] = replace_once(texts[0], '(m->flags & (PTE_PCD | PTE_PWT)) !=\n                (ledger_surfaces[m->sid].cache == LEDGER_CACHE_UC ? PTE_PCD : 0)', '0')
            if name == 'generation ignored':
                texts[0] = replace_once(texts[0], 'ledger_surfaces[m->sid].gen != m->generation', '0')
        texts = [s.replace('irq_save()', '0').replace('irq_restore(saved)', '(void)saved')
                   .replace('irq_restore(flags)', '(void)flags') for s in texts]
        texts[0] = texts[0].replace('pgalloc_alloc_phys(', 'host_lease_alloc(').replace('pgalloc_free_n_owner(', 'host_lease_free(')
        texts[0] = texts[0].replace(
            '((u32 *)P2V(as->lease_pt_phys[k]))[(va >> PAGE_SHIFT) % PTE_COUNT] = 0;',
            '{ host_pte_clear(va); ((u32 *)P2V(as->lease_pt_phys[k]))[(va >> PAGE_SHIFT) % PTE_COUNT] = 0; }')
        texts[1] = texts[1].replace('int pgalloc_free_n_owner(', 'int host_pgalloc_free_n_owner(')
        texts[1] = texts[1].replace('((u8 *)P2V(sf->first * PAGE_SIZE))[p]',
                                  '((u8 *)host_surface_pointer(sf->first * PAGE_SIZE))[p]')
        for name, source in zip(('paging_host_source.c', 'pgalloc_host_source.c', 'lease_host_source.c'), texts):
            (tmp / name).write_text(source)
        includes = ['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'include',
                    'arch/x86', 'platform/pc98', 'kernel', 'lib', 'exec', 'sdk/include/os32')] + ['-I' + str(tmp)]
        exe = tmp / 'lease'
        subprocess.run(['gcc', *FLAGS, '-DPHYSMEM_HOST_TEST=1', '-nostdlib', '-static',
                        '-no-pie', '-Wl,--gc-sections', *includes, str(ROOT / 'tools/tests/lease_host.c'),
                        str(ROOT / 'kernel/physmem.c'), '-o', str(exe)], check=True)
        return host32.run([str(exe)], capture_output=True, text=True, timeout=60)

p = run()
print(p.stdout, end='')
if p.returncode:
    print(p.stderr)
    raise SystemExit(p.returncode)
if '--mutate' in sys.argv:
    for mutant in MUTANTS:
        p = run(mutant)
        if p.returncode == 0:
            raise SystemExit('SURVIVED: ' + mutant[0])
        assert p.returncode in (1, 2) and ('FAIL:' in p.stdout or
            'ORDER: free before TLB synchronization' in p.stdout), (mutant[0], p.returncode, p.stdout)
        if mutant[0] in ('free before active TLB synchronization',
                          'backing free before active TLB synchronization'):
            assert p.returncode == 2 and 'ORDER: free before TLB synchronization' in p.stdout, p.stdout
            kind = 'backing' if mutant[0].startswith('backing') else 'PT'
            assert 'ORDER kind: ' + kind in p.stdout, p.stdout
        print('runtime RED:', mutant[0], 'rc=', p.returncode)
        if mutant[0] in ('free before active TLB synchronization',
                          'backing free before active TLB synchronization'):
            print(p.stdout, end='')
    print(f'{len(MUTANTS)}/{len(MUTANTS)} runtime RED; compile failures 0')
