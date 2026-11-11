#!/usr/bin/env python3
"""Compare the shipped RH reference with the software ROP renderer."""
import argparse
from pathlib import Path
import tempfile
import host32
from kernel_includes import include_flags
from mutpar import build_in_tree
ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'userland/tests/gfx2_rh.c'
CHECK = dict(name='gfx2-rh-host', tier=1, needs=[], budget=20,
    covers=['userland/tests/gfx2_rh.c', 'gfx/v2/sw_rop.c', 'gfx/v2/format.c',
            'gfx/v2/sw_table.c', 'gfx/v2/*.h', 'include/*.h', 'sdk/include/os32/*.h',
            'arch/*/arch_copy.h', 'mm/*.h', 'tools/tests/gfx2_rh_ref_host.c',
            'tools/tests/test_gfx2_rh.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py'])
MUTATIONS = [
    ('mono-bit-order', '0x80>>px', '1<<px'),
    ('rop-index', '(((p>>bit)&1)<<2)|(((s>>bit)&1)<<1)|((d>>bit)&1)', '(((d>>bit)&1)<<2)|(((s>>bit)&1)<<1)|((p>>bit)&1)'),
    ('pattern-origin', 'ref_pat(x,y,pat,fg,bg)', 'ref_pat(x-3,y-5,pat,fg,bg)'),
    ('gf-expectation', '{GFX2_OP_FILL,0x5a,0,GFX2_FMT_XRGB8888,GFX2_POOL_VRAM,GFX2_HOW_NOSUPPORT,GFX2_HOW_SW}', '{GFX2_OP_FILL,0x5a,0,GFX2_FMT_XRGB8888,GFX2_POOL_VRAM,GFX2_HOW_HW,GFX2_HOW_SW}'),
]
def observe(work, mutation=None):
    work.mkdir()
    edits = {}
    if mutation:
        name, old, new = mutation
        source = (ROOT / SOURCE).read_text()
        assert source.count(old) == 1, name
        edits[SOURCE] = source.replace(old, new)
    exe = work / 'guest'
    build_in_tree(ROOT, work, edits, [['gcc', '-m32', '-std=gnu11', '-O1',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin', '-fno-pie',
        '-no-pie', '-nostdlib', '-static', '-ffunction-sections', '-fdata-sections',
        '-Wl,--gc-sections', '-I'+str(ROOT), '-I'+str(ROOT/'gfx/v2'), *include_flags(ROOT),
        str(ROOT/'tools/tests/gfx2_rh_ref_host.c'), str(ROOT/'gfx/v2/sw_rop.c'),
        str(ROOT/'gfx/v2/format.c'), str(ROOT/'gfx/v2/sw_table.c'), '-o', str(exe)]])
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gfx2-rh-') as td:
        work = Path(td)
        assert observe(work/'normal') == 0
        if args.mutate:
            for mutation in MUTATIONS:
                assert observe(work/mutation[0], mutation) == 1, mutation[0]
                print('RED runtime', mutation[0], flush=True)
            assert observe(work/'restored') == 0

def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
if __name__ == '__main__': main()
