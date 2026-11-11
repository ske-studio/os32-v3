#!/usr/bin/env python3
"""C7: real lock.c, freestanding ILP32 runtime and product-line mutations."""
CHECK = dict(name='gfx2-lock-host', tier=1, needs=[], budget=15,
    covers=['gfx/v2/lock.c', 'gfx/v2/gfx2_int.h', 'include/gfx_hal.h',
            'include/gfx_hal2.h', 'include/gfx_sync.h', 'include/types.h',
            'include/type_contract.h', 'include/memmap.h', 'mm/paging.h',
            'mm/pgalloc.h', 'lib/kstring.h', 'sdk/include/os32/os32_gfx2.h',
            'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/gfx2_lock_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'include/copy_units.h', 'arch/x86/arch_copy.h', 'arch/ppc/arch_copy.h'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    target = work / 'lock.c'
    target.write_text(source)
    exe = work / 'lock-test'
    host32.build(['gcc', '-m32', '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-I' + str(ROOT / 'gfx/v2'), *include_flags(ROOT),
        str(target), str(ROOT / 'tools/tests/gfx2_lock_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/lock.c').read_text()
    mutations = [
        ('revoke-unmap', 'if (s->staging || s->map_as) rc = staging_unmap(s);', '(void)0;'),
        ('revoke-alias', 'if (!(s->flags & GFX2_SF_ALIAS)) return OS32_ERR_INVAL;', '(void)0;'),
        ('rollback', 'if (mapped && paging_lease_unmap(as, base, mapped))', 'if (0)'),
        ('discard-wait', 'rc = gfx2_cpu_begin(s, access, GFX2_LOCK_DL_US);',
         'rc = flags & GFX2_LOCK_DISCARD ? 0 : gfx2_cpu_begin(s, access, GFX2_LOCK_DL_US);'),
        ('staging-branch', 'if (ctx) {\n        n =', 'if (0) {\n        n ='),
        ('discard-zero', 'kmemset(s->staging, 0, n * PAGE_SIZE);', '(void)0;'),
        ('staging-drop', 'if (s->staging && staging_unmap(s)) rc = OS32_ERR_IO;', '(void)0;'),
        ('keep-pages', 'if (s->staging && !(s->flags & GFX2_SF_KEEP))', 'if (s->staging)'),
        ('unmap-state', 'if (paging_lease_unmap(s->map_as, s->map_va, s->map_npages)) return OS32_ERR_IO;',
         '(void)paging_lease_unmap(s->map_as, s->map_va, s->map_npages);'),
        ('visibility', 'return s->kva || (s->map_as && paging_current_cr3() == s->map_as->pd_phys);',
         'return s->kva != 0;'),
    ]
    with tempfile.TemporaryDirectory(prefix='gfx2-lock-') as td:
        work = Path(td)
        rc = observe(work, source)
        assert rc == 0, f'normal runtime rc={rc}'
        print('GREEN C7 mappings, waits, staging lifecycle and failures')
        if args.mutate:
            for name, anchor, replacement in mutations:
                assert source.count(anchor) == 1, name
                rc = observe(work, source.replace(anchor, replacement))
                assert rc > 0, f'{name}: expected runtime RED, rc={rc}'
                print(f'RED {name}: runtime rc={rc}')
    print('PASS gfx2-lock-host')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
