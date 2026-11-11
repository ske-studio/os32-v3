#!/usr/bin/env python3
"""G1 freestanding adapter, ROP state isolation and G4 deadline/rc boundary."""
from pathlib import Path
import argparse
import tempfile
import host32
from kernel_includes import include_flags
from mutpar import build_in_tree
ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'drivers/video/geforce/gf_v2_ops.c'
MUTATIONS = (
    ('stopped-unregister', 'if (gf_v2_inst.ch && gf_v2_inst.ch->state == GF2_OFF)', 'if (0)'),
    ('lost-check', 'if (g->lost || !g->g3) return OS32_ERR_NOSYS;', 'if (g->lost && !g->g3) return OS32_ERR_NOSYS;'),
    ('restore-error', 'if (restore) return OS32_ERR_IO;', '(void)restore;'),
    ('restore', 'int restore = gf3_rop3_set(g->g3, subc, 0xcc, 0, dst.fmt, dl);', 'int restore = 0;'),
    ('restore-on-error', 'if (custom) {\n            int restore', 'if (custom && !rc) {\n            int restore'),
    ('pack-mono', '((i % 4) * 8)', '((3 - i % 4) * 8)'),
    ('key', 'GFX2_F_SRCKEY | GFX2_F_DSTKEY | GFX2_F_ALPHA_PIXEL', 'GFX2_F_ALPHA_PIXEL'),
)
def observe(work, mutation=None):
    edits = {}
    if mutation:
        _, old, new = mutation
        text = (ROOT / SOURCE).read_text()
        assert text.count(old) == 1
        edits[SOURCE] = text.replace(old, new)
    work = work / (mutation[0] if mutation else "normal-" + str(len(list(work.iterdir()))))
    work.mkdir()
    exe = work / 'ops'
    sources = ['tools/tests/gf_v2_ops_host.c', 'drivers/video/geforce/gf_v2_weak.c',
               'drivers/video/geforce/gf_v2_fence.c', 'drivers/video/geforce/gf_v2_life.c', 'hwdev/gf/accel_rop3.c', 'hwdev/gf/accel_rop.c']
    build_in_tree(ROOT, work, edits, [['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-I'+str(ROOT), *include_flags(ROOT), *[str(ROOT / p) for p in sources], '-o', str(exe)]])
    return host32.run([str(exe)]).returncode

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf-v2-ops-') as td:
        work=Path(td)
        assert observe(work)==0
        print('GREEN G1 state, packing, dispatch, fence, deadlines, errors and lifecycle')
        if args.mutate:
            for mutation in MUTATIONS:
                assert observe(work, mutation)==1, mutation[0]
                print('RED', mutation[0])
            assert observe(work)==0
            print('GREEN restored source')
if __name__=='__main__': main()
