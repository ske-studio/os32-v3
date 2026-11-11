#!/usr/bin/env python3
"""DISPLAY alias readback through real VRAM validation and M2MF packets."""
from pathlib import Path
import argparse
import tempfile
import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

CHECK = dict(name='gf-v2-readback-host', tier=1, needs=[], budget=15,
    covers=['drivers/video/geforce/gf_v2_ops.c', 'drivers/video/geforce/gf_v2_life.c',
            'drivers/video/geforce/*.h', 'hwdev/gf/**', 'gfx/v2/*.h', 'include/*.h',
            'arch/*/arch_copy.h', 'sdk/include/os32/*.h',
            'tools/tests/gf_v2_readback_host.c', 'tools/tests/test_gf_v2_readback.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py'])
ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'drivers/video/geforce/gf_v2_ops.c'
MUTATIONS = (
    ('alias-identity', 's.reservation_id = v.reservation_id;', 's.reservation_id = 0;'),
    ('alias-generation', 's.generation = v.generation;', 's.generation = 0;'),
    ('alias-range', 's.bytes && s.bytes <= v.bytes - (s.off - v.off)', 's.bytes'),
    ('alias-only', '(ex->src->flags & GFX2_SF_ALIAS) && ex->src->pool == GFX2_POOL_VRAM',
                   'ex->src->pool == GFX2_POOL_VRAM'),
)


def observe(folder, mutation=None):
    folder.mkdir()
    edits = {}
    if mutation:
        _, old, new = mutation
        source = (ROOT / SOURCE).read_text()
        assert source.count(old) == 1
        edits[SOURCE] = source.replace(old, new)
    sources = ['tools/tests/gf_v2_readback_host.c', 'drivers/video/geforce/gf_v2_life.c',
               'hwdev/gf/accel_surface.c', 'hwdev/gf/accel_transfer.c',
               'hwdev/gf/accel_m2mf.c', 'hwdev/gf/vram_view.c']
    exe = folder / 'readback'
    build_in_tree(ROOT, folder, edits, [['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra',
        '-Werror', '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-I' + str(ROOT), *include_flags(ROOT),
        *[str(ROOT / p) for p in sources], '-o', str(exe)]])
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf-v2-readback-') as td:
        work = Path(td)
        assert observe(work / 'normal') == 0
        print('GREEN DISPLAY alias -> reservation -> VRAM/PCI M2MF -> pixels')
        if args.mutate:
            for mutation in MUTATIONS:
                rc = observe(work / mutation[0], mutation)
                assert rc > 0, (mutation[0], rc)
                print('RED', mutation[0], 'runtime rc=' + str(rc))
            assert observe(work / 'restored') == 0
            print('GREEN restored')


def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3', '-B', __file__,
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    main()
