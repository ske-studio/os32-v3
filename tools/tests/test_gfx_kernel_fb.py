"""T2e e4 real three-backend framebuffer/source/lease ILP32 integration.
Normal controls run on every requested runner; mutants on the first runner.
Hardware discovery and port I/O are stubbed, lifecycle and ledger are real.
"""
import argparse
import hashlib
import pathlib
import re
import subprocess
import tempfile
import host32
from mutpar import run_ordered
ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET_SRCS = ['gfx/gfx_core.c', 'gfx/backend_pc98.c', 'gfx/backend_pegc.c',
               'gfx/backend_cirrus.c', 'gfx/gfx_vram.c', 'gfx/gfx_scroll.c',
               'exec/surface_query.c', 'exec/lease.c', 'exec/access_walk.c',
               'exec/redir_access.c', 'kernel/shlib.c', 'kernel/paging.c',
               'kernel/pgalloc.c', 'kernel/sys.c', 'kernel/physmem.c']
MUTANTS = [
 ('fixed-bb', 'u8 *bb_b, *bb_r, *bb_g, *bb_i;',
  'u8 *bb_b = P2V_CONST(MEM_GFX_BB_BASE), *bb_r, *bb_g, *bb_i;',
  'FAIL !bb_b && !bb_r && !bb_g && !bb_i && !gfx_backend_pc98.bb_base'),
 ('rounded-stride', '.plane_offset = {0, GFX_BPL * GFX_HEIGHT, 2 * GFX_BPL * GFX_HEIGHT,\n                       3 * GFX_BPL * GFX_HEIGHT}',
  '.plane_offset = {0, GVRAM_PLANE_SIZE, 2 * GVRAM_PLANE_SIZE, 3 * GVRAM_PLANE_SIZE}',
  'FAIL bb_r-bb_b == GFX_PLANE_SZ'),
 ('display-wb', '.format = GFX_BB_PACKED8, .planes = 1, .cache = LEDGER_CACHE_UC,\n    .perm_max = LEDGER_PERM_RW',
  '.format = GFX_BB_PACKED8, .planes = 1, .cache = LEDGER_CACHE_WB,\n    .perm_max = LEDGER_PERM_RW',
  'FAIL ledger_surface_find(LEDGER_SF_PEGC,LEDGER_ROLE_DISPLAY) != 0'),
 ('display-absent', '!ledger_surface_create(&gfx_pegc_display, 0)', '1',
  'FAIL ledger_surface_find(LEDGER_SF_PEGC,LEDGER_ROLE_DISPLAY) != 0'),
 ('preinit-backend', 'selected ? gfx_sf_backend() : LEDGER_SF_PC98', 'gfx_sf_backend()',
  'FAIL fb.width == sf->width && fb.height == height'),
 ('init200-rebind', '    gfx_bind_client(); /* bind before init_200; retain 400-line plane stride */', '',
  'FAIL fb.width == sf->width && fb.height == height'),
 ('preinit-screen-pc98', '    if (g_backend && g_backend->query)\n        g_backend->query((GFX_ScreenInfo *)out);',
  '    (gfx_started ? g_backend : &gfx_backend_pc98)->query((GFX_ScreenInfo *)out);',
  'FAIL si.format == selected_info.format && si.format == client->format'),
 ('preinit-public-pc98', '(void)gfx_client_framebuffer(&kernel, 1);',
  '(void)gfx_kernel_framebuffer(&kernel);',
  'FAIL public_fb.width == si.width && public_fb.height == si.height && public_fb.pitch == client->pitch'),
 ('fallback-bind', '            gfx_bind_client(); /* bind init failure fallback */', '',
  'FAIL bb[0] == (u8 *)P2V(MEM_GFX_BB_BASE) && bb[1] == bb[0]+GFX_PLANE_SZ'),
 ('packed-bind', '        gfx_bind_client(); /* bind packed init */', '',
  'FAIL bb[i] == want'),
]

def replace_function(body, signature, replacement):
    start = body.index(signature + '\n{')
    brace = body.index('{', start)
    level = 1
    end = brace + 1
    while level:
        level += (body[end] == '{') - (body[end] == '}')
        end += 1
    return body[:brace] + '{\n' + replacement + '\n}' + body[end:]

def run_case(mutation, runner, extra_changes=(), fixture_body=None, source_texts=None, object_cache=None):
    with tempfile.TemporaryDirectory(prefix='os32-e4-') as name:
        tmp = pathlib.Path(name)
        arch = (ROOT/'tools/tests/host_arch/arch_io.h').read_text().replace(
            'static unsigned int host_arch_if = 0x202U;', 'extern unsigned int host_arch_if;')
        (tmp/'arch_io.h').write_text(arch)
        (tmp/'platform_io.h').write_text('''#ifndef PLATFORM_IO_H
#define PLATFORM_IO_H
static inline unsigned int inp(unsigned int p) { static unsigned n; (void)p;return 4U | ((++n & 1U) ? 0x20U : 0); }
static inline void outp(unsigned int p,unsigned int v) {(void)p;(void)v;}
static inline unsigned int inpw(unsigned int p) {return inp(p);}
static inline void outpw(unsigned int p,unsigned int v) {outp(p,v);}
static inline unsigned long inpd(unsigned int p) {return inp(p);}
static inline void outpd(unsigned int p,unsigned long v) {outp(p,v);}
static inline void insw_rep(unsigned int p,void *b,unsigned int n) {(void)p;(void)b;(void)n;}
static inline void io_wait(void) {}
static inline void io_wait_n(int n) {(void)n;}
#endif
''')
        sources = []
        for path in TARGET_SRCS:
            body = source_texts[path] if source_texts is not None else (ROOT/path).read_text()
            for unit, old, new in extra_changes:
                if unit == path:
                    assert body.count(old) == 1, (unit, old, body.count(old))
                    body = body.replace(old, new)
            if path == 'gfx/gfx_core.c':
                if mutation:
                    _, old, new, _ = mutation
                    assert body.count(old) == 1, (mutation[0], body.count(old))
                    body = body.replace(old,new)
                    if mutation[0] == 'packed-bind':
                        old = '        gfx_reinit_pending = 0;\n        gfx_bind_client();'
                        assert body.count(old) == 1
                        body = body.replace(old, '        gfx_reinit_pending = 0;\n        if (gfx_sf_backend() == LEDGER_SF_PC98) gfx_bind_client();')
                body = body.replace('*(volatile u8 *)P2V_IO(arch)', '(arch, PEGC_BIOS_ARCH_EXTGFX)')
                body += '\nint host_select_and_init(void) { return gfx_select_and_init_backend(); }\n'
            if path == 'gfx/backend_pegc.c':
                body = replace_function(body,'int pegc_identify(void)','    return 1;')
                body = replace_function(body,'static int pegc_probe(void)', '''
    extern int host_pegc_available;
    if (!s_probed) {
        const struct ledger_surface *sf=ledger_surface_find(LEDGER_SF_PEGC,LEDGER_ROLE_CLIENT);
        s_probed=1; s_probe_ok=sf != 0;
        if (sf) s_bb_phys=sf->first*PAGE_SIZE;
    }
    return s_probe_ok && host_pegc_available;''')
            if path == 'gfx/backend_cirrus.c':
                body = replace_function(body,'int cirrus_identify(void)','    return 1;')
                body = replace_function(body,'static int cirrus_probe(void)', '''
    extern int host_cirrus_available;
    if (!s_probed) {
        s_probed=1;s_glue=&wab_glue_xe10;
        s_probe_ok=ledger_surface_find(LEDGER_SF_CIRRUS,LEDGER_ROLE_CLIENT) != 0;
    }
    return s_probe_ok && host_cirrus_available;''')
            # Preserve source-relative include semantics for private copies.
            body = re.sub(r'#include "(\.\./[^"\n]+)"',
                          lambda m: '#include "'+str((ROOT/path).parent/m[1])+'"', body)
            if path in ('kernel/paging.c','kernel/pgalloc.c','kernel/sys.c'):
                (tmp/(pathlib.Path(path).stem+'_host_source.c')).write_text(body)
            else:
                dest = tmp/(pathlib.Path(path).stem+'.c');dest.write_text(body);sources.append(dest)
        fixture = 'unsigned int host_arch_if = 0x202U;\n#include "gfx_kernel_fb_host.c"\n'
        (tmp/'fixture.c').write_text(fixture_body or fixture)
        sources.append(tmp/'fixture.c')
        flags=['gcc','-std=gnu11','-m32','-march=i386','-ffreestanding','-fno-pie',
               '-fno-stack-protector','-ffunction-sections','-fdata-sections',
               '-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
               '-DPHYSMEM_HOST_TEST=1','-I'+str(tmp)]
        flags += ['-I'+str(ROOT/p) for p in ('tools/tests','tools/tests/host_arch','include',
                  'arch/x86','platform/pc98','kernel','exec','gfx','drivers','lib','fs',
                  'kapi','lib/sqlite3','sdk/include/os32')]
        exe=tmp/'test'
        inputs = sources
        if object_cache is not None:
            inputs = []
            for src in sources:
                content = src.read_bytes()
                if src.name == 'fixture.c':
                    content += b''.join((tmp/(unit+'_host_source.c')).read_bytes()
                                        for unit in ('paging','pgalloc','sys'))
                obj = object_cache/(hashlib.sha256(content).hexdigest()+'.o')
                if not obj.exists():
                    result = subprocess.run(flags+['-c',str(src),'-o',str(obj)],
                                            capture_output=True,text=True)
                    assert result.returncode == 0, result.stderr
                inputs.append(obj)
        compiled=subprocess.run(flags+['-nostdlib','-static','-no-pie','-Wl,--gc-sections',
            *map(str,inputs),'-o',str(exe)],capture_output=True,text=True)
        assert compiled.returncode == 0, compiled.stderr
        return host32.run([str(exe)],runner=runner,capture_output=True,text=True,timeout=60)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner',choices=['native','qemu'],default='native')
    parser.add_argument('--mutate',action='store_true')
    args=parser.parse_args()
    result=run_case(None,args.runner)
    print(result.stdout+result.stderr,end='')
    assert result.returncode == 0,result.returncode
    if args.mutate:
        def one(m):
            result=run_case(m,args.runner)
            assert result.returncode == 1 and m[3] in result.stdout, (m[0],result.returncode,result.stdout,result.stderr)
            return 'RED '+m[0]
        for message in run_ordered(one,MUTANTS): print(message)
        print(f'PASS {len(MUTANTS)}/{len(MUTANTS)} runtime mutants')
if __name__ == '__main__': main()
