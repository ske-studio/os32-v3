"""Exercise the M2 mapping transaction with a freestanding page-table fixture."""
CHECK = dict(name='paging-gfx2-host', tier=1, budget=5,
             covers=['mm/paging_gfx2.inc', 'mm/paging.c', 'tools/tests/paging_gfx2_host.c',
                     'tools/tests/test_paging_gfx2.py', 'tools/tests/host32.py'], needs=[])
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = ROOT / 'mm/paging_gfx2.inc'
MUTATIONS = [
    ('present', ' & PTE_PRESENT)\n                goto fail;', ' & 0)\n                goto fail;'),
    ('rollback', 'pgalloc_free_n_owner(as->owner, pending[k] / PAGE_SIZE, 1);',
     'if (pending[k] == PAGE_SIZE) pgalloc_free_n_owner(as->owner, pending[k] / PAGE_SIZE, 1);'),
]

def main():
    original = PRODUCT.read_text()
    with tempfile.TemporaryDirectory(prefix='paging-gfx2-') as name:
        tmp = Path(name)
        exe = tmp / 'host'
        def run(body):
            (tmp / 'paging_gfx2.inc').write_text(body)
            subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                            '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                            '-I' + str(tmp), str(ROOT / 'tools/tests/paging_gfx2_host.c'),
                            '-o', str(exe)], check=True)
            return host32.run([str(exe)], runner='qemu').returncode
        assert run(original) == 0
        print('GREEN runner=qemu', flush=True)
        if '--mutate' in sys.argv:
            for label, old, new in MUTATIONS:
                assert original.count(old) == 1
                assert run(original.replace(old, new)) == 1, label
                print('MUTATE runtime RED ' + label, flush=True)
                assert run(original) == 0
                print('RESTORED GREEN ' + label, flush=True)

if __name__ == '__main__':
    main()

def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_paging_gfx2.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
