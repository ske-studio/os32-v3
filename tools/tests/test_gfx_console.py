#!/usr/bin/env python3
"""Console target: real HAL surface lifetime and synchronous scanout contract."""
CHECK = dict(name='gfx-console-host', tier=1, needs=[], budget=8,
    covers=['gfx/gfx_console.c', 'gfx/gfx_console.h', 'gfx/v2/surface.c',
            'gfx/v2/gfx2_int.h', 'include/gfx_hal.h', 'include/gfx_hal2.h',
            'include/gfx_sync.h', 'include/types.h', 'include/memmap.h',
            'include/gfx_mode.h', 'include/gfx_format.h', 'include/callconv.h',
            'arch/x86/arch_abi.h', 'include/abi.h', 'include/config.h',
            'mm/pgalloc.h', 'lib/kstring.h', 'sdk/include/os32/**',
            'tools/tests/gfx_console_host.c', 'tools/tests/gfx2_surface_host.c',
            'tools/tests/test_gfx_console.py', 'tools/tests/host32.py'])
from pathlib import Path
import argparse
import tempfile
import host32
ROOT=Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('surface connection', '    gfx2_scan_ref_swap(a, head, s);', '    (void)0;'),
    ('boundary guard', 'w > s->w - x || h > s->h - y', '0'),
    ('generation', 'a->gen != t->generation ||', ''),
    ('lost', 'a->gen != t->generation || a->state != GFX2_AD_OK ||', 'a->gen != t->generation || 0 ||'),
    ('pitch', 's->w, s->h, s->pitch, 0', 's->w, s->h, s->w, 0'),
    ('scan lifetime', '            gfx2_head_detach(a, t->head);', '            (void)0;'),
]
def main():
    p=argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true'); args=p.parse_args()
    body=(ROOT/'gfx/gfx_console.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work=Path(td)
        fixture=(ROOT/'tools/tests/gfx2_surface_host.c').read_text().replace('static u8 ram[128];','static u8 ram[4096];')
        (work/'surface_fixture.inc').write_text(fixture)
        def run(text):
            (work/'console.c').write_text(text)
            exe=work/'test'
            flags=['gcc','-m32','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function',
                   '-ffreestanding','-fno-builtin','-fno-pie','-no-pie','-fno-stack-protector','-nostdlib','-static']
            flags += ['-I'+str(ROOT/x) for x in ('gfx','gfx/v2','include','mm','lib','arch/x86','sdk/include/os32')]
            host32.build(flags+['-I'+str(work),str(work/'console.c'),str(ROOT/'gfx/v2/surface.c'),
                               str(ROOT/'tools/tests/gfx_console_host.c'),'-o',str(exe)],check=True)
            return host32.run([str(exe)],runner='qemu').returncode
        assert run(body)==0
        print('GREEN console: pitch, bounds, attach, sync, LOST, close/rebind',flush=True)
        if args.mutate:
            for name,old,new in MUTATIONS:
                assert body.count(old)==1,(name,body.count(old))
                rc=run(body.replace(old,new)); assert 0<rc<128,(name,rc)
                print(f'RED runtime {name}: rc={rc}',flush=True)
    return 0
def run(ctx):
    import subprocess
    return subprocess.call(['python3','-B','tools/tests/test_gfx_console.py',*(['--mutate'] if ctx.mutate else [])],cwd=ctx.root)
if __name__=='__main__': raise SystemExit(main())
