"""T2e e6: real SDK and kernel CLIENT leases, checked drawing and void bridge."""
import argparse
import hashlib
import pathlib
import re
import tempfile
import time
from mutpar import run_ordered
from test_gfx_kernel_fb import run_case, TARGET_SRCS as KERNEL_SRCS, ROOT

# docs/tasks/v3/TASK_T2D_T2H.md §2 e6; literal closure for the inventory.
TARGET_SRCS = [
    'gfx/gfx_core.c',
    'gfx/backend_pc98.c',
    'gfx/backend_pegc.c',
    'gfx/backend_cirrus.c',
    'gfx/gfx_vram.c',
    'gfx/gfx_scroll.c',
    'exec/surface_query.c',
    'exec/lease.c',
    'exec/access_walk.c',
    'exec/redir_access.c',
    'kernel/shlib.c',
    'kernel/paging.c',
    'kernel/pgalloc.c',
    'kernel/sys.c',
    'kernel/physmem.c',
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
    'lib/utf8_internal.h',
]
assert set(KERNEL_SRCS) <= set(TARGET_SRCS)
SDK_SRCS = [p for p in TARGET_SRCS if (p.startswith("userland/") or p == "lib/utf8.c") and p.endswith(".c")]
HEADERS = [p for p in TARGET_SRCS if p.endswith(".h")]
CORE='userland/lib/gfx/libos32gfx_core.c'
MUTANTS = [
 ('skip-failed-pool-init', [(CORE,'    if (gfx_api && !gfx_pools_initialized) {',
   '    if (!rc && gfx_api && !gfx_pools_initialized) {')], 'FAIL sdk_allocations==1'),
 ('reset-live-pools', [(CORE,'gfx_api && !gfx_pools_initialized','gfx_api')],
   'FAIL next_surface && next_surface!=kept_surface'),
 ('bridge-trusted-abort', [('gfx/gfx_core.c','caller.origin == CALLER_USER && current >= APP_ID_MIN',
   'current >= APP_ID_MIN')], 'FAIL gfx_bridge_fail_count==failed+1 && !slot.abort_req'),
 ('attach-two-tokens', [(CORE,'if (gfx_ready && gfx_view.token &&','if (0 && gfx_ready && gfx_view.token &&'),
   (CORE,'    libos32gfx_detach();\n    if (!desc.ref.generation','    if (!desc.ref.generation')],
   'FAIL !libos32gfx_attach_checked() && live()==1 && space.leases[0].token==token'),
 ('skip-generation', [(CORE,'gfx_ref.generation == desc.ref.generation','1')],
   'FAIL !libos32gfx_check() && live()==1 && space.leases[0].token!=token'),
 ('partial-no-release', [(CORE,'view.planes[i] != view.base + desc.plane_offset[i]) goto invalid;',
   'view.planes[i] != view.base + desc.plane_offset[i]) return OS32_ERR_INVAL;')],
   'FAIL libos32gfx_attach_checked()==OS32_ERR_INVAL && !live()'),
 ('draw-after-failure', [('userland/lib/gfx/draw/gfx_draw.c',
   'void gfx_clear(u8 color)\n{\n    if (!gfx_ready) return;',
   'void gfx_clear(u8 color)\n{')], 'FAIL draws==old && asm_calls==oldasm'),
 ('retain-old-bb', [(CORE,'    gfx_ref = desc.ref;\n    gfx_fb = fb;',
   '    static u8 *old_bb;\n    gfx_ref = desc.ref;\n    gfx_fb = fb;\n    if (old_bb) gfx_fb.planes[0] = old_bb; else old_bb = gfx_fb.planes[0];')],
   'FAIL !libos32gfx_check() && gfx_fb.planes[0]!=old && live()==2'),
 ('bridge-user-alias', [('gfx/gfx_core.c','fb.planes[i] = (u8 *)(held->base + sf->plane_offset[i]);',
   'fb.planes[i] = kernel.planes[i];')],
   'FAIL (u32)bridge->planes[0]==space.leases[1].base'),
 ('compat-no-reuse', [('gfx/gfx_core.c','l->generation == source.refs[0].generation) held = l;',
   'l->generation == source.refs[0].generation) (void)l;')],
   'FAIL live()==1 && space.leases[0].token==token'),
 ('trusted-query', [(CORE,'if (gfx_cpl() == 0 || !gfx_attach_port) {','if (!gfx_attach_port) {')],
   'FAIL !libos32gfx_attach_checked() && queries==q && legacy_calls==1 && !live()'),
 ('bridge-no-abort', [('gfx/gfx_core.c','slot && slot->state == APP_STATE_RUNNING) slot->abort_req = 1;', 'slot && slot->state == APP_STATE_RUNNING) (void)slot;')],
   'FAIL slot.abort_req && gfx_bridge_fail_count==fails+1'),
 ('bridge-dirty-output', [('gfx/gfx_core.c','    fb = (GFX_Framebuffer){0};\n    if (valid)',
   '    fb.planes[0] = (u8 *)1;\n    if (valid)')],
   'FAIL !fb->width && !fb->height && !fb->planes[0]'),
 ('metadata-in-pte-check', [('exec/lease.c','expected |= l->flags & ~AS_LEASE_GFX_COMPAT;',
   'expected |= l->flags;')], 'FAIL !lease_check(&space)'),
 ('present-no-query', [(CORE,'    if (libos32gfx_check() || !gfx_ready) return;',
   '    if (!gfx_ready) return;')], 'FAIL gfx_ready && live()==1'),
]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner',choices=['native','qemu'],default='native')
    p.add_argument('--mutate',action='store_true')
    a=p.parse_args()
    texts={path:(ROOT/path).read_text() for path in TARGET_SRCS}
    digest={path:hashlib.sha256(text.encode()).digest() for path,text in texts.items()}
    # SDK and kernel deliberately export the same drawing names. Rename only
    # SDK symbols in the private test copies, never KernelAPI member names.
    names=set(re.findall(r'\b(gfx_\w+)\s*\(', '\n'.join(texts[x] for x in SDK_SRCS+HEADERS)))
    names.discard('gfx_cpl')
    pattern=re.compile(r'(?<!->)(?<!\.)\b('+ '|'.join(sorted(names))+r')\b')
    def renamed(body): return pattern.sub(lambda m:'sdk_'+m[0],body)
    with tempfile.TemporaryDirectory(prefix='os32-e6-objects-') as directory:
        def run(changes):
            def stage(tmp,sources):
                for path in SDK_SRCS+HEADERS:
                    body=texts[path]
                    for unit,old,new in changes:
                        if unit==path:
                            assert body.count(old)==1,(unit,old,body.count(old))
                            body=body.replace(old,new)
                    if path.endswith('libos32gfx_core.c'):
                        body=body.replace('return cs & 3U;', 'extern unsigned int host_sdk_cpl; return host_sdk_cpl;')
                    if path.endswith('gfx_dump.c'):
                        # Existing sizeof loop is signed; target build allows it.
                        body='#pragma GCC diagnostic ignored "-Wsign-compare"\n'+body
                    body=renamed(body)
                    dest=tmp/pathlib.Path(path).name
                    dest.write_text(body)
                    if path in SDK_SRCS: sources.append(dest)
            return run_case(None,a.runner,[c for c in changes if c[0] in KERNEL_SRCS],
                            '#include "gfx_attach_host.c"\n',texts,pathlib.Path(directory),stage)
        result=run([])
        print(result.stdout+result.stderr,end='')
        assert result.returncode==0,result.returncode
        if a.mutate:
            def one(m):
                start=time.monotonic();result=run(m[1])
                assert result.returncode==1 and m[2] in result.stdout,(m[0],result.returncode,result.stdout,result.stderr)
                return f'RED {m[0]} {time.monotonic()-start:.2f}s'
            for msg in run_ordered(one,MUTANTS): print(msg)
            print(f'PASS {len(MUTANTS)}/{len(MUTANTS)} runtime mutants')
    assert all(hashlib.sha256((ROOT/path).read_bytes()).digest()==d for path,d in digest.items())
if __name__=='__main__': main()
