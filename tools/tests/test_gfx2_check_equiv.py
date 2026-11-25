#!/usr/bin/env python3
"""Characterize INDEX8 check_op and submit on PEGC and OF framebuffer backends."""
import argparse
from pathlib import Path
import tempfile
from test_gfx2_i1 import ROOT, observe

EXTRA = sorted(str(path.relative_to(ROOT)) for path in (ROOT / 'gfx/v2').glob('sw_*.c')
               if path.name != 'sw_table.c')
EXTRA += ['sys/hal/gfx/v2/check_op.c', 'sys/hal/gfx/v2/submit.c', 'sys/hal/gfx/v2/format.c',
          'sys/hal/gfx/sync/sync_bind.c', 'lib/divdi3.c']
NATIVE = ['sys/platform/pc98/video/pegc_v2.c', 'sys/dev/video/of_fb/of_fb_v2.c',
          'sys/dev/video/of_fb/backend_of_fb.c']
MUTATIONS = [
    ('alpha capability', 'sys/hal/gfx/v2/check_op.c',
     '(op->op == GFX2_OP_ALPHA && df == GFX2_FMT_INDEX8)', '0'),
    ('shadow dispatch', 'sys/hal/gfx/v2/check_op.c',
     'shadow(a, ex->dst)) {\n        *how = GFX2_HOW_SW;',
     'shadow(a, ex->dst)) {\n        *how = GFX2_HOW_HW;'),
    ('filter capability', 'sys/hal/gfx/v2/check_op.c',
     '(op->op == GFX2_OP_STRETCH && (op->flags & GFX2_F_FILTER) && df == GFX2_FMT_INDEX8)', '0'),
]

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for mutation in [None, *(MUTATIONS if args.mutate else [])]:
        with tempfile.TemporaryDirectory() as work:
            observe(Path(work), mutation, fixture='gfx2_check_equiv_host.c', extra=EXTRA,
                    native=NATIVE, cflags=('-nostdinc',))
