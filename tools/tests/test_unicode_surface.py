"""T3d1b: private product table, two SDK KAPI callers, real query/lease/ledger.

The host maps separate lease VAs; no hardware or emulator is accessed.
Mutants must fail at the named runtime assertion (compile failures are errors).
"""
import host32
import argparse
import pathlib
import re
import tempfile
import subprocess
from test_unicode_lookup import byte_array
from test_gfx_attach import TARGET_SRCS as ATTACH_SRCS, SDK_SRCS, HEADERS, CORE
from test_gfx_kernel_fb import ROOT, run_case
from mutpar import run_ordered

FIXTURE = 'tools/tests/unicode_surface_host.c'
SYSTEM = 'exec/system_surface.c'
UTF8 = 'lib/utf8.c'
TARGET_SRCS = [
    'gfx/gfx_core.c',
    'drivers/video/pc98/backend_pc98.c',
    'drivers/video/pc98/backend_pegc.c',
    'drivers/video/cirrus/backend_cirrus.c',
    'drivers/video/cirrus/cirrus_identify.c',
    'gfx/gfx_vram.c',
    'gfx/gfx_scroll.c',
    'exec/surface_query.c',
    'exec/lease.c',
    'exec/access_walk.c',
    'exec/redir_access.c',
    'mm/shlib.c',
    'mm/paging.c',
    'mm/pgalloc.c',
    'kernel/sys.c',
    'mm/physmem.c',
    'userland/lib/gfx/libos32gfx_core.c',
    'userland/lib/gfx/draw/gfx_draw.c',
    'userland/lib/gfx/draw/gfx_blt.c',
    'userland/lib/gfx/draw/gfx_surface.c',
    'userland/lib/gfx/draw/gfx_sprite.c',
    'userland/lib/gfx/draw/gfx_rotate.c',
    'userland/lib/gfx/draw/gfx_raster.c',
    'userland/lib/gfx/draw/gfx_dump.c',
    'userland/lib/gfx/geom/gfx_fill.c',
    'userland/lib/gfx/geom/gfx_circle.c',
    'userland/lib/gfx/geom/gfx_bezier.c',
    'userland/lib/gfx/libos32gfx.h',
    'userland/lib/gfx/libgfx_internal.h',
    'userland/lib/gfx/libgfx_attach_internal.h',
    'userland/lib/math/libos32math.h',
    'lib/utf8.c',
    'lib/utf8.h',
    'kernel/console/unicode_file.c',
    'kernel/console/unicode_file.h',
    'exec/system_surface.c',
    'exec/system_surface.h',
    'tools/tests/unicode_surface_host.c',
    'tools/tests/gfx_kernel_fb_host.c',
    'tools/tests/gfx_attach_host.c',
]
assert set(ATTACH_SRCS) <= set(TARGET_SRCS)
MUTANTS = [
    ('sdk-no-kapi', UTF8, 'return kapi->unicode_to_jis(cp);', '(void)cp; return 0;', 'FAIL unicode_to_jis(0x3041)==0x2421'),
    ('tvram-gui', 'exec/surface_query.c', '        if (gui) return OS32_ERR_INVAL;', '        if (0) return OS32_ERR_INVAL;', 'FAIL tvram_gui_denied'),
    ('slot-unready', 'mm/pgalloc.c', '    gfx_surface_unready &= ~(1U << i);', '', 'FAIL slot_ready'),
]


@host32.control_session
def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner', choices=['native', 'qemu'], default='native')
    p.add_argument('--mutate', action='store_true')
    a = p.parse_args()
    host32.begin_control(a.mutate, a.runner, ROOT)
    originals = {x: (ROOT/x).read_text() for x in TARGET_SRCS}
    names = set(re.findall(r'\b(gfx_\w+)\s*\(', '\n'.join(originals[x] for x in SDK_SRCS+HEADERS)))
    names.discard('gfx_cpl')
    first = re.compile(r'(?<!->)(?<!\.)\b('+'|'.join(sorted(names))+r')\b')
    globals_ = ['gfx_api', 'gfx_fb', 'gfx_ready', 'gfx_packed', 'gfx_dirty_suppress', 'gfx_attach_port']
    second = re.compile(r'(?<!->)(?<!\.)(?<!struct )\b('+'|'.join(sorted(names | set(globals_)))+
                        r'|libos32gfx_\w+|utf8_\w+(?=\s*\()|unicode_to_\w+(?=\s*\())\b')
    with tempfile.TemporaryDirectory(prefix='os32-e8a-objects-') as cache:
        def run(m=None):
            texts = dict(originals)
            if m:
                assert texts[m[1]].count(m[2]) == 1, (m[0],texts[m[1]].count(m[2]))
                texts[m[1]] = texts[m[1]].replace(m[2], m[3])
            def stage(tmp, sources):
                for shlib in (False, True):
                    destdir = tmp/'shl' if shlib else tmp
                    destdir.mkdir(exist_ok=True)
                    for path in SDK_SRCS+HEADERS:
                        body = texts[path]
                        if path == CORE:
                            body = body.replace('return cs & 3U;', 'extern unsigned int host_sdk_cpl; return host_sdk_cpl;')
                        if path.endswith('gfx_dump.c'):
                            body = '#pragma GCC diagnostic ignored "-Wsign-compare"\n'+body
                        body = (second.sub(lambda x: 'shl_'+x[0], body) if shlib else
                                first.sub(lambda x: 'sdk_'+x[0], body))
                        if shlib and path == UTF8:
                            body = '#include "os32_kapi_shared.h"\n#undef kapi\n#define kapi shl_kapi\n' + body
                        dest = destdir/pathlib.Path(path).name
                        dest.write_text(body)
                        if path in SDK_SRCS: sources.append(dest)
                for path in (SYSTEM, 'kernel/console/unicode_file.c'):
                    dest=tmp/('kernel_'+pathlib.Path(path).name)
                    dest.write_text(texts[path]);sources.append(dest)
                generator=tmp/'gen_unicode'
                subprocess.run(['gcc','-std=gnu11','-I'+str(ROOT),'-I'+str(ROOT/'include'),
                                str(ROOT/'tools/gen_unicode.c'),'-o',str(generator)],check=True)
                pair_file=tmp/'pairs.bin'
                subprocess.run([str(generator),'--pairs',str(pair_file)],check=True)
                (tmp/'unicode_pairs.inc').write_text(byte_array('generated_pairs',pair_file.read_bytes()))
                footing = texts['tools/tests/gfx_kernel_fb_host.c'].replace('int con_sink_is_enabled(void) { return 0; }',
                            'static int host_gui;\nint con_sink_is_enabled(void) { return host_gui; }')
                (tmp/'gfx_kernel_fb_host.c').write_text(footing)
                prefix = texts['tools/tests/gfx_attach_host.c'].split('static void run(void) __attribute__((used));')[0]
                fixture = texts[FIXTURE]
                (tmp/'fixture.c').write_text(prefix+'\n'+fixture)
            return run_case(None,a.runner,fixture_body='/* staged */',source_texts=texts,
                            object_cache=pathlib.Path(cache),stage=stage)
        with host32.control(a.mutate, a.runner, ROOT) as normal:
            if normal:
                r=run();print(r.stdout+r.stderr,end='');assert r.returncode==0,r.returncode
        if a.mutate:
            def one(m):
                r=run(m)
                assert r.returncode==1 and m[4] in r.stdout,(m[0],r.returncode,r.stdout,r.stderr)
                return 'RED '+m[0]
            for msg in run_ordered(one,MUTANTS): print(msg)
            print(f'PASS {len(MUTANTS)}/{len(MUTANTS)} runtime mutants')

if __name__=='__main__': main()
