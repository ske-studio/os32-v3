"""e7: real Rust wait, Painter, geometry, static wrappers and gdi control flow."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import os32api_host
from mutpar import run_ordered
ROOT=Path(__file__).resolve().parents[2]
GUI='userland/rust/libos32gui/src/'
GDI='userland/rust/gdi_test/src/lib.rs'
TARGET_SRCS=[GUI+x+'.rs' for x in ('client','clip','draw','ffi','gstate','surface','utf8core')]+[GDI,GUI+'shlib.rs','sdk/rust/os32api/src/lib.rs','sdk/rust/os32api/src/gui/stub.rs']
MUTANTS=[
 ('shlib-no-unicode',GUI+'shlib.rs','    unsafe { crate::ffi::libos32gfx_unicode_init(); }','', 'shlib Unicode acquisition missing'),
 ('surface-size-stale',GUI+'surface.rs','pub fn surface_size(id: SurfaceId) -> (i32, i32) {\n    crate::gstate::screen_info_cached();','pub fn surface_size(id: SurfaceId) -> (i32, i32) {','first surface_size is stale'),
 ('base-clip-stale',GUI+'clip.rs','pub fn set_base_clip(surface: SurfaceId, rect: Rect) -> i32 {\n    crate::gstate::screen_info_cached();','pub fn set_base_clip(surface: SurfaceId, rect: Rect) -> i32 {','base clip keeps old geometry'),
 ('wait-no-check',GUI+'client.rs','    check_gfx()?;','', 'return check missing'),
 ('painter-no-gate',GUI+'draw.rs','if unsafe { core::ptr::read_volatile(core::ptr::addr_of!(ffi::gfx_ready)) } == 0 {\n            return Painter','if false {\n            return Painter','Painter gate missing'),
 ('cache-not-invalidated',GUI+'client.rs','    // Also invalidate on failure: recovery must not reuse old geometry.\n    crate::gstate::st().screen_valid = false;','','stale screen cache'),
 ('gdi-one-instance',GDI,'let shlib_rc = os32api::gui::stub::check_gfx();','let shlib_rc = 0;','gdi must stop both renderers'),
 ('gdi-no-rollback',GDI,'        os32api::gfx::detach();','', 'gdi static rollback missing'),
]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mutate',action='store_true');a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='os32-e7-rust-') as d:
        base=Path(d);rlib=os32api_host.build(base)
        def run(m=None):
            dest=base/(m[0] if m else 'normal');dest.mkdir()
            texts={p:(ROOT/p).read_text() for p in TARGET_SRCS}
            if m:
                assert texts[m[1]].count(m[2])==1,(m[0],texts[m[1]].count(m[2]))
                texts[m[1]]=texts[m[1]].replace(m[2],m[3])
            root='#![allow(dead_code, unused_imports)]\n'
            for path in TARGET_SRCS[:7]:
                unit=Path(path).stem;(dest/(unit+'.rs')).write_text(texts[path]);root+=f'mod {unit};\n'
            root+=(ROOT/'tools/tests/gui_reattach_host.rs').read_text()
            init=texts[GUI+'shlib.rs'].split('pub extern "C" fn os32gui_shlib_init(')[1].split('\n/* ===')[0]
            root+='\nuse os32api::KernelAPI;\nstatic mut SHLIB_INIT_OK: bool=false;\n'
            root+='mod cfgro { pub fn set_kapi(_: *mut core::ffi::c_void) {} }\n'
            root+='pub extern "C" fn os32gui_shlib_init('+init

            helper=texts[GDI].split('fn check_both_gfx() -> bool {')[1].split('\n#[no_mangle]')[0]
            root+='\nfn check_both_gfx() -> bool {'+helper.replace('os32api::','app_api::')
            (dest/'main.rs').write_text(root)
            c=subprocess.run(['rustc','--edition=2021','--test',str(dest/'main.rs'),'--extern','os32api='+str(rlib),'-o',str(dest/'test')],capture_output=True,text=True)
            assert c.returncode==0,c.stderr
            return subprocess.run([str(dest/'test'),'--test-threads=1'],capture_output=True,text=True)
        r=run();print(r.stdout+r.stderr,end='');assert r.returncode==0
        if a.mutate:
            def one(m):
                r=run(m);assert r.returncode==101 and m[4] in r.stdout,(m[0],r.stdout,r.stderr)
                return 'RED '+m[0]
            for msg in run_ordered(one,MUTANTS):print(msg)
            print(f'PASS {len(MUTANTS)}/{len(MUTANTS)} runtime mutants')
if __name__=='__main__':main()
