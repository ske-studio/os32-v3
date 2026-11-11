#!/usr/bin/env python3
"""C15 teardown contracts; compile the product TU without libc."""
CHECK = dict(name='gfx2-owner-release-host', tier=1, needs=[], budget=10,
    covers=['gfx/v2/owner_release.c', 'gfx/v2/gfx2_int.h', 'gfx/v2/gfx2_exec.h',
            'include/gfx_hal.h', 'include/gfx_hal2.h', 'include/gfx_sync.h',
            'include/types.h', 'include/memmap.h', 'include/type_contract.h',
            'include/abi.h', 'include/callconv.h', 'include/copy_units.h',
            'arch/*/arch_copy.h', 'arch/*/arch_abi.h', 'mm/pgalloc.h',
            'mm/paging.h', 'include/appmem_types.h', 'sdk/include/os32/os32_gfx2.h',
            'tools/tests/gfx2_owner_release_host.c', 'tools/tests/test_gfx2_owner_release.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    tu = work / 'owner_release.c'
    tu.write_text(source)
    exe = work / 'owner-release'
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-I' + str(ROOT / 'gfx/v2'), *include_flags(ROOT), str(tu),
        str(ROOT / 'tools/tests/gfx2_owner_release_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/owner_release.c').read_text()
    mutations = [
        ('quarantine replaced by destroy', 'if (gfx2_surf_quarantine(s)) poison = 1;',
         'if (gfx2_surf_destroy(s)) poison = 1;'),
        ('AS map detach removed', '            gfx2_map_detach(s);', '            (void)s;'),
        ('unmap failure does not poison', '                poison = 1;', '                poison = 0;'),
        ('staging drop removed', '        gfx2_staging_drop(s);', '        (void)s;'),
        ('KEEP drain guard removed', '        if (s->flags & GFX2_SF_KEEP) {', '        if (0) {'),
        ('pending head ignored', '                head_owned(a->head[h].pend_sid, as->owner))',
         '                0)'),
        ('transfer failure ignored', '    return rc;', '    return 0 * rc;'),
        ('vaspace drop removed', '    gfx2_vaspace_drop(as);', '    (void)as;'),
        ('read fence ignored', 'gfx2_fence_done(a, s->last_read)', '1'),
    ]
    with tempfile.TemporaryDirectory(prefix='gfx2-owner-') as td:
        work = Path(td)
        assert observe(work, source) == 0, 'baseline runtime failure'
        print('GREEN C15 contracts (qemu/ILP32, no libc)')
        if args.mutate:
            for label, anchor, replacement in mutations:
                assert source.count(anchor) == 1, (label, source.count(anchor))
                rc = observe(work, source.replace(anchor, replacement))
                assert rc == 1, (label, rc)
                print(f'RED {label}: runtime rc={rc}')
    print('PASS C15 owner release')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
