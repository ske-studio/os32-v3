"""T2d d3: real paging/allocator/shlib/access source, ILP32 host MMU/IRQ.
Only a fixed source closure is copied; compile failures never count as RED.
"""
import argparse
import hashlib
import pathlib
import subprocess
import tempfile
import time
from mutpar import run_ordered
ROOT = pathlib.Path(__file__).resolve().parents[2]
FILES = {
    'paging': 'kernel/paging.c', 'pgalloc': 'kernel/pgalloc.c',
    'shlib': 'kernel/shlib.c', 'access_walk': 'exec/access_walk.c',
    'redir_access': 'exec/redir_access.c',
}
MUTANTS = [
    ('access_walk', 'as_va_to_pa(as->pd_phys, va, &result)', 'as_va_to_pa(paging_current_cr3(), va, &result)', 'registrant PD write'),
    ('access_walk', 'as_va_to_pa_read(as->pd_phys, va, &result)', 'as_va_to_pa_read(paging_current_cr3(), va, &result)', 'registrant PD read'),
    ('access_walk', 'va - (u32)MEM_SHM_BASE < MEM_SHM_SIZE', '1', 'SHM upper bound'),
    ('access_walk', '(sf->backing != LEDGER_SB_RAM && sf->backing != LEDGER_SB_FIXED_RAM)', '0', 'valid MMIO/VRAM lease'),
    ('access_walk', 'if (d & PTE_PS) return 0;', '', 'PS in both walk layers'),
    ('access_walk', '!ledger_surface_validate(sf)', '0', 'fixed RAM ledger'),

    ('access_walk', 'if (expected != paging_registered_pt(va))', 'if (0)', 'master registry'),
    ('access_walk', 'if (result != va)', 'if (0)', 'shared identity'),
    ('access_walk', 'if (!expected || table != expected)', 'if (!expected)', 'fake PT'),
    ('access_walk', 'if (!pgalloc_page_owned(expected / PAGE_SIZE, as->owner))', 'if (0)', 'PT owner'),
    ('access_walk', '!pgalloc_page_owned(as->pd_phys / PAGE_SIZE, as->owner)', '0', 'PD owner'),
    ('paging', '(pde & need) != need', '(pde & (need & ~PTE_RW)) != (need & ~PTE_RW)', 'PDE RW'),
    ('paging', '(pte & need) != need', '(pte & (need & ~PTE_RW)) != (need & ~PTE_RW)', 'PTE RW'),
    ('access_walk', 'table != expected', '0', 'shared PT'),
    ('access_walk', 'pgalloc_page_owned(frame / PAGE_SIZE, as->owner)', '1', 'private PFN owner'),
    ('access_walk', 'if (frame == as->pd_phys)', 'if (0)', 'PD as payload'),
    ('access_walk', 'if (frame == as->app_pt_phys[i])', 'if (0)', 'APP PT as payload'),
    ('access_walk', 'if (frame == as->lease_pt_phys[i])', 'if (0)', 'lease PT as payload'),
    ('access_walk', 'sf->gen != l->generation', '0', 'surface generation'),
    ('access_walk', '!(l->flags & PTE_RW)', '0', 'RO lease authority'),
    ('access_walk', 'frame != (sf->first + (va - l->base) / PAGE_SIZE) * PAGE_SIZE', '0', 'lease PFN'),
    ('shlib', 'g_pages[page] == frame', '1', 'shlib exact backing'),
    ('access_walk', '!write && shlib_read_page(va, frame)', 'shlib_read_page(va, frame)', 'shlib output'),
]


def run(sources, mutant=None):
    with tempfile.TemporaryDirectory(prefix='os32-d3-') as directory:
        tmp = pathlib.Path(directory)
        for key, body in sources.items():
            if mutant and mutant[3] == 'PS in both walk layers' and key == 'paging':
                body = body.replace(' || (pde & PTE_PS)', '')
            if mutant and key == mutant[0]:
                _, old, new, _ = mutant
                assert body.count(old) == 1, (old, body.count(old))
                body = body.replace(old, new)
            (tmp / (key + '_host_source.c')).write_text(body)
        exe = tmp / 'walk'
        cmd = ['gcc', '-std=gnu11', '-m32', '-march=i386', '-ffreestanding',
               '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
               '-Wall', '-Wextra', '-Werror', '-DPHYSMEM_HOST_TEST=1', '-nostdlib',
               '-static', '-no-pie', '-Wl,--gc-sections',
               *['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'include',
                  'arch/x86', 'platform/pc98', 'kernel', 'exec', 'fs', 'lib', 'sdk/include/os32')],
               '-I' + str(tmp), str(ROOT / 'tools/tests/access_walk_host.c'),
               str(ROOT / 'kernel/physmem.c'), '-o', str(exe)]
        subprocess.run(cmd, check=True, capture_output=True, text=True)
        return subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    sources = {k: (ROOT / v).read_text() for k, v in FILES.items()}
    hashes = {p: hashlib.sha256((ROOT / p).read_bytes()).digest() for p in FILES.values()}
    result = run(sources)
    print(result.stdout + result.stderr, end='')
    assert result.returncode == 0
    if args.mutate:
        def one(m):
            start = time.monotonic()
            r = run(sources, m)
            assert r.returncode != 0 and 'FAIL:' in r.stdout, (m[3], r.stdout, r.stderr)
            return m[3], time.monotonic() - start
        for name, seconds in run_ordered(one, MUTANTS):
            print(f'RED (runtime): {name} ({seconds:.2f}s)')
        print(f'MUTATIONS {len(MUTANTS)}/{len(MUTANTS)} runtime RED')
    assert all(hashlib.sha256((ROOT / p).read_bytes()).digest() == h for p, h in hashes.items())

if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as e:
        print(e.stdout or '', e.stderr or '')
        raise
