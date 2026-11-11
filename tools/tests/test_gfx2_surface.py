#!/usr/bin/env python3
"""C4 surface lifetimes; real product TU, no libc, runtime mutation evidence."""
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    path = work / 'surface.c'
    path.write_text(source)
    exe = work / 'surface'
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'gfx/v2'),
        '-I' + str(ROOT / 'mm'), '-I' + str(ROOT / 'lib'),
        '-I' + str(ROOT / 'arch/x86'), '-I' + str(ROOT / 'sdk/include/os32'),
        str(path), str(ROOT / 'tools/tests/gfx2_surface_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/surface.c').read_text()
    mutations = [
        ('heap rollback', 'if (surf_backing_free(a, s)) goto keep; /* creation rollback */', 'if (0) goto keep;'),
        ('bytes overflow', 'pitch > a->caps.max_pitch || pitch > ~0UL / h', 'pitch > a->caps.max_pitch'),
        ('locked transition', 'old == GFX2_ST_LOCKED_RO || old == GFX2_ST_LOCKED_RW', 'old == GFX2_ST_LOCKED_RW'),
        ('scan reference', 'if (s->scan_ref || !surf_done(s)) {', 'if (!surf_done(s)) {'),
        ('alias generation', 'surf_generation(s, s->gen + 1);', 'surf_generation(s, s->gen);'),
        ('screen owner', 'c.owner != owner || c.app_id != gfx_screen_owner()', 'c.owner != owner'),
        ('alias free', 'if (s->flags & GFX2_SF_ALIAS) return OS32_ERR_INVAL; /* borrowed backing */', 'if (0) return OS32_ERR_INVAL;'),
        ('KEEP on failure', 's->flags |= GFX2_SF_KEEP;\n    /* C7', 's->flags |= 0;\n    /* C7'),
        ('SYSMEM alloc bypass', 'if (pool != GFX2_POOL_SYSMEM) {', 'if (1) {'),
        ('SYSMEM release bypass', 's->pool != GFX2_POOL_SYSMEM && a->be->release', 'a->be->release'),
        ('IO retains mapping', '/* C7 leaves map_* unchanged on IO;', 's->map_as = NULL; /* C7 leaves map_* unchanged on IO;'),
        ('release order', 'if (s->pool != GFX2_POOL_SYSMEM && a->be->release(a->ctx, s)) goto keep;\n    if (surf_backing_free(a, s)) goto keep;',
         'if (surf_backing_free(a, s)) goto keep;\n    if (s->pool != GFX2_POOL_SYSMEM && a->be->release(a->ctx, s)) goto keep;'),
    ]
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        rc = observe(work, source)
        assert rc == 0, f'baseline rc={rc}'
        print('GREEN surface lifetime, rollback, aliases, states and deferred free', flush=True)
        if args.mutate:
            for name, anchor, replacement in mutations:
                assert source.count(anchor) == 1, (name, source.count(anchor))
                mutated = source.replace(anchor, replacement)
                rc = observe(work, mutated)
                assert rc > 0, f'{name}: expected runtime RED, rc={rc}'
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
