#!/usr/bin/env python3
"""Run the shipped I3 guest through fake GF/PEGC and real Trident C16."""
import argparse
from pathlib import Path
import tempfile
import host32
from kernel_includes import include_flags
from mutpar import build_in_tree
ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'userland/tests/gfx2_i3.c'
CHECK = dict(name='gfx2-i3-guest-host', tier=1, needs=[], budget=20,
    covers=['userland/tests/gfx2_i3.c', 'gfx/v2/v1_shim.c', 'gfx/v2/*.h', 'include/*.h',
            'sdk/include/os32/*.h', 'lib/kstring_c.c', 'lib/kstring.h',
            'arch/*/arch_copy.h', 'mm/*.h',
            'tools/tests/gfx2_i3_guest_host.c', 'tools/tests/test_gfx2_i3_guest.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py'])
MUTATIONS = [
    ('current-mode', 'if (mode.w == desc.w && mode.h == desc.h)', 'if (1)'),
    ('shim-alias', 'mode.id, display);', 'mode.id, front);'),
    ('mode-expectation', 'if (info->backend_kind == OS32_SURFACE_TRIDENT) {', 'if (0) {'),
    ('lease-order', 'CALL("before-mode-unlease", api->surface_unlease(token)); token = 0;', '(void)token;'),
    ('fresh-display', 'CALL("shim-display", api->gfx2_screen_surface(a, 0, GFX2_ROLE_DISPLAY, &display));', '(void)display;'),
    ('new-token', 'token = lease.token;\n        } else {', 'token = 0;\n        } else {'),
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
        str(ROOT / 'tools/tests/gfx2_i3_guest_host.c'),
        str(ROOT / 'gfx/v2/v1_shim.c'), str(ROOT / 'lib/kstring_c.c'), '-o', str(exe)]])
    return host32.run([str(exe)]).returncode

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gfx2-i3-guest-') as td:
        work=Path(td)
        assert observe(work/'normal') == 0
        if args.mutate:
            for mutation in MUTATIONS:
                assert observe(work/mutation[0], mutation)==1, mutation[0]
                print('RED runtime', mutation[0], flush=True)
            assert observe(work/'restored')==0

def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
if __name__=='__main__': main()
