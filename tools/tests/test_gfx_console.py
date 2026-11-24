#!/usr/bin/env python3
"""Console target: real HAL surface lifetime and synchronous scanout contract."""
CHECK = dict(name='gfx-console-host', tier=1, needs=[], budget=20,
    covers=['tools/tests/of_fb_osri_fake.h', 'gfx/gfx_console.c', 'gfx/gfx_console.h', 'gfx/v2/surface.c',
            'gfx/v2/gfx2_int.h', 'include/gfx_hal.h', 'include/gfx_hal2.h',
            'include/gfx_sync.h', 'include/types.h', 'include/memmap.h',
            'include/gfx_mode.h', 'include/gfx_format.h', 'include/callconv.h',
            'arch/x86/arch_abi.h', 'include/abi.h', 'include/config.h',
            'mm/pgalloc.h', 'lib/kstring.h', 'sdk/include/os32/**',
            'tools/tests/gfx_console_host.c', 'tools/tests/gfx2_surface_host.c',
            'tools/tests/test_gfx_console.py', 'tools/tests/host32.py',
            'tools/tests/fbcon_contract_host.c', 'tools/tests/kernel_includes.py',
            'kernel/console/fbcon.c', 'kernel/console/fbcon.h',
            'kernel/console/console_backend.h', 'platform/powermac/console.c',
            'platform/powermac/pm_console.h', 'platform/powermac/rh_observe.h',
            'drivers/video/of_fb/**', 'drivers/video/pc98/backend_pc98.c',
            'drivers/video/kcg/kcg_module.h', 'gfx/v2/v1_shim.c',
            'gfx/v2/enum.c', 'gfx/v2/caps.c', 'gfx/palette.h', 'gfx/gfx_internal.h'],
    basis=['HAL_V2_DESIGN §6-2', 'HAL_V2_DESIGN §7-3', 'CONSOLE_DUAL_DESIGN §2-1'],
)
from pathlib import Path
import argparse
import tempfile
import host32
from kernel_includes import include_flags
ROOT=Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('owner kind', 'ledger_owner_new(LEDGER_KIND_MODULE,', 'ledger_owner_new(LEDGER_KIND_PERSIST,'),
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
                   '-ffreestanding','-fno-builtin','-fno-pie','-no-pie','-fno-stack-protector','-nostdlib','-nostdinc','-static']
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
        contract(work, args.mutate)
    return 0

def contract(work, mutate):
    console=(ROOT/'gfx/gfx_console.c').read_text()
    fbcon=(ROOT/'kernel/console/fbcon.c').read_text()
    variants=[('baseline', console, fbcon)]
    if mutate:
        for name,old,new in [
            ('stale scanout', 'a->head[t->head].scan_sid != t->sid', '0'),
            ('stale generation', 'a->gen != t->generation ||', ''),
            ('dirty omitted', '    gfx2_dirty_add(s, &rect);', '    (void)0;')]:
            assert console.count(old)==1, name
            variants.append((name, console.replace(old,new), fbcon))
        old='    if (gfx_console_lock(c->target, &pixels)) { c->ready = 0; return; }'
        assert fbcon.count(old)==1
        variants.append(('unlock before paint', console, fbcon.replace(old, old +
            '\n    gfx_console_unlock(c->target, x * 8, c->top + y * 16, w * 8, h * 16);')))
        variants.append(('control',console,fbcon))
    fixture=(ROOT/'tools/tests/gfx2_surface_host.c').read_text().replace(
        'static u8 ram[128];','static u8 ram[16384];')
    (work/'surface_fixture.inc').write_text(fixture)
    sources=['gfx/v2/surface.c','gfx/v2/caps.c','gfx/v2/enum.c','gfx/v2/v1_shim.c',
             'drivers/video/of_fb/backend_of_fb.c','drivers/video/of_fb/of_fb_v2.c',
             'drivers/video/pc98/backend_pc98.c','platform/powermac/console.c',
             'tools/tests/fbcon_contract_host.c']
    for name,ct,ft in variants:
        (work/'console.c').write_text(ct); (work/'fbcon.c').write_text(ft)
        exe=work/'contract'
        host32.build(['gcc','-m32','-std=gnu11','-Wall','-Wextra','-Werror',
            '-Wno-unused-function','-ffreestanding','-fno-builtin','-fno-pie',
            '-no-pie','-fno-stack-protector','-nostdlib','-nostdinc','-static',
            '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
            '-I'+str(work),'-I'+str(ROOT/'gfx/v2'),*include_flags(ROOT),
            str(work/'console.c'),str(work/'fbcon.c'),
            *(str(ROOT/p) for p in sources),'-o',str(exe)],check=True)
        result=host32.run([str(exe)],runner='qemu',capture_output=True,text=True)
        red=name not in ('baseline','control')
        assert result.returncode==int(red) and (not red or result.stdout.startswith('FAIL ')), (name,result)
        print(('RED runtime ' if red else 'GREEN ')+'fbcon contract '+name+
              ': rc='+str(result.returncode)+' '+result.stdout.strip(),flush=True)

def run(ctx):
    import subprocess
    return subprocess.call(['python3','-B','tools/tests/test_gfx_console.py',*(['--mutate'] if ctx.mutate else [])],cwd=ctx.root)
if __name__=='__main__': raise SystemExit(main())
