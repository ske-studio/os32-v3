"""T2b: actual paging/ledger/lease ILP32 runtime and compiled runtime mutants."""
import pathlib
import subprocess
import tempfile
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-ffreestanding', '-fno-pie',
         '-fno-stack-protector', '-Wall', '-Wextra', '-Werror']
SOURCES = ('kernel/paging.c', 'kernel/pgalloc.c', 'exec/lease.c')
MUTANTS = [
    ('active TLB synchronization missing', 2, 'if (*root != paging_kernel_pd_phys()) paging_load_cr3(paging_kernel_pd_phys());', '(void)root;'),
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

def run(changes=None):
    with tempfile.TemporaryDirectory(prefix='os32-lease-') as tmpdir:
        tmp = pathlib.Path(tmpdir)
        texts = [(ROOT / p).read_text() for p in SOURCES]
        if changes:
            name, index, old, new = changes
            assert texts[index].count(old) == 1, name
            texts[index] = texts[index].replace(old, new)
            if name == 'permission ignored':
                texts[0] = texts[0].replace('((m->flags & PTE_RW) && ledger_surfaces[m->sid].perm_max != LEDGER_PERM_RW)', '0')
            if name == 'PCD lost':
                texts[0] = texts[0].replace('(m->flags & (PTE_PCD | PTE_PWT)) !=\n                (ledger_surfaces[m->sid].cache == LEDGER_CACHE_UC ? PTE_PCD : 0)', '0')
            if name == 'generation ignored':
                texts[0] = texts[0].replace('ledger_surfaces[m->sid].gen != m->generation', '0')
        texts = [s.replace('irq_save()', '0').replace('irq_restore(saved)', '(void)saved')
                   .replace('irq_restore(flags)', '(void)flags') for s in texts]
        texts[0] = texts[0].replace('pgalloc_alloc_phys(', 'host_lease_alloc(').replace('pgalloc_free_n_owner(', 'host_lease_free(')
        for name, source in zip(('paging_host_source.c', 'pgalloc_host_source.c', 'lease_host_source.c'), texts):
            (tmp / name).write_text(source)
        includes = ['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'include',
                    'arch/x86', 'platform/pc98', 'kernel', 'lib', 'exec')] + ['-I' + str(tmp)]
        exe = tmp / 'lease'
        subprocess.run(['gcc', *FLAGS, '-DPHYSMEM_HOST_TEST=1', '-nostdlib', '-static',
                        '-no-pie', *includes, str(ROOT / 'tools/tests/lease_host.c'),
                        str(ROOT / 'kernel/physmem.c'), '-o', str(exe)], check=True)
        return subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)

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
        print('runtime RED:', mutant[0], 'rc=', p.returncode)
    print(f'{len(MUTANTS)}/{len(MUTANTS)} runtime RED; compile failures 0')
