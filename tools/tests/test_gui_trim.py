"""g2s: execute the production run_vt, app hook trampoline and allocator.

T7b-b3 S4: run_vt also takes GUI_EV_FONT before dispatch (font reload + every
window invalidated, no TRIM_DONE) and returns the glyph arena (font::trim) in
the trim batch before the hook. Phase 2 runs the production libos32gui font.rs
(tools/tests/gui_font_host.rs) over the real os32-font crate and fonts."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import os
import json
import sys
import os32api_host
ROOT=Path(__file__).resolve().parents[2]
APP='userland/rust/libos32gui/src/app.rs'
STUB='userland/rust/libos32gui_stub/src/lib.rs'
ALLOC='sdk/rust/os32api/src/lib.rs'
MUTATIONS=[
    ('hook-twice-per-batch',APP,'let pages = h();','let pages = h(); let _ = h();','hook-twice-per-batch'),
    ('trim-in-handler',APP,'s().trim_batch = true;','s().trim_batch = true; if let Some(h) = s().hook { h(); }','trim-in-handler'),
    ('forward-trim-to-app',APP,'s().trim_batch = true;','s().trim_batch = true; dispatch(app, ui, &buf[i]);','forward-trim-to-app'),
    ('done-before-hook',APP,'if let Some(h) = s().hook {','let _ = client::call(GUI_OP_TRIM_DONE, s().trim_epoch); if let Some(h) = s().hook {','done-before-hook'),
    ('trampoline-no-guard',STUB,'os32api::gui::trim_enter();','','hook allocation must not yield'),
    # T7b-b3 S4 (b3③): EV_FONT is taken before dispatch and is not a trim.
    ('font-ev-dispatched',APP,'            } else if buf[i].kind == GUI_EV_FONT {','            } else if false {','font-ev-dispatched'),
    ('font-ev-trim-done',APP,'                crate::font::reload();\n','                crate::font::reload();\n                s().trim_batch = true;\n','font-ev-trim-done'),
    ('font-trim-skipped',APP,'            crate::font::trim();\n','','font-trim-skipped'),
]
FONT='userland/rust/libos32gui/src/font.rs'
# Phase 2 (font.rs over the real crate): b3④ arena, b3⑤/P2-8 borrow, P2-10, §2-3, §2-5.
FONT_MUTATIONS=[
    ('no-zero-fill',FONT,'        unsafe { ptr::write_bytes(p, 0, size) };\n','','zero-fill'),
    ('trim-keeps-arena',FONT,'            unsafe { (os32api::api().mem_free)(arena.as_mut_ptr()) };\n','','trim-frees-arena'),
    ('trim-ends-borrow',FONT,'    if e.trim().is_err() {','    e.end();\n    if e.trim().is_err() {','trim-while-busy'),
    ('gen-ignored',FONT,'Some((_, gen)) if gen != f.open_gen =>','Some((_, gen)) if false && gen != f.open_gen =>','gen-reload'),
    ('retry-never',FONT,'    if !matches!(e.state(), State::KcgOnly(Reason::Nospc) | State::KcgOnly(Reason::OpenFailed)) {','    if true {','retry'),
    ('arena-every-text',FONT,'    if !e.cache().has_arena() && !st().arena_blocked {','    if !st().arena_blocked {','arena-once'),
    ('fp-scope-no-restore',FONT,'''        core::arch::asm!("fnclex", options(nomem, nostack, preserves_flags));
        core::arch::asm!("fldcw word ptr [{0}]", in(reg) &saved as *const u16,
                         options(nostack, preserves_flags));''','''        core::arch::asm!("fnclex", options(nomem, nostack, preserves_flags));''','fp-scope-restore'),
]
FONT_MOCKS='''#![allow(dead_code, unused_imports, static_mut_refs)]
mod cfgro {
    static mut CFG: Option<(i32, i32)> = None;
    static mut READS: u32 = 0;
    pub fn set(v: Option<(i32, i32)>) { unsafe { CFG = v; } }
    pub fn reads() -> u32 { unsafe { READS } }
    pub fn font_settings(_: i32, _: i32) -> Option<(i32, i32)> { unsafe { READS += 1; CFG } }
}
mod client {
    static mut LOG: Vec<String> = Vec::new();
    pub fn dbg_print_num(m: &[u8], v: i32) { unsafe { LOG.push(format!("{} {}", String::from_utf8_lossy(m), v)); } }
    pub fn log() -> Vec<String> { unsafe { LOG.clone() } }
}
'''

def build_font_crate(work):
    """Host rlib of userland/lib/font (and its vendored ttf-parser)."""
    env={**os.environ,'CARGO_TARGET_DIR':str(work/'font-target')}
    b=subprocess.run(['cargo','rustc','--lib','--release','--offline','--manifest-path',str(ROOT/'userland/lib/font/Cargo.toml'),
                      '--message-format=json'],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    msgs=[json.loads(l) for l in b.stdout.splitlines() if l.startswith('{')]
    assert b.returncode==0,b.stderr
    rlib=next(f for m in msgs if m.get('reason')=='compiler-artifact' and m['target']['name']=='os32_font'
              for f in m['filenames'] if f.endswith('.rlib'))
    return Path(rlib),work/'font-target/release/deps'

def font_env(work):
    sys.path.insert(0,str(Path(__file__).resolve().parent))
    subprocess.run(['python3',str(ROOT/'tools/fetch_fonts.py'),'--family','inconsolata'],check=True)
    from test_font_outline import shinonome
    return {**os.environ,'OS32_FONT_SHINONOME':str(shinonome(work)),'OS32_ROOT':str(ROOT)}

def font_phase(mutate):
    with tempfile.TemporaryDirectory(prefix='t7b-gui-font-') as d:
        work=Path(d);rlib=os32api_host.build(work);font_rlib,deps=build_font_crate(work);env=font_env(work)
        text=(ROOT/FONT).read_text()
        def run(m=None):
            src=text
            name='normal'
            if m:
                name,_,old,new,_=m
                assert src.count(old)==1,name
                src=src.replace(old,new)
            unit=work/name;unit.mkdir()
            (unit/'font.rs').write_text(src)
            code=FONT_MOCKS+'#[path="font.rs"] mod font;\n'+(ROOT/'tools/tests/gui_font_host.rs').read_text()
            (unit/'main.rs').write_text(code)
            c=subprocess.run(['rustc','--edition=2021','--test',str(unit/'main.rs'),'--extern','os32api='+str(rlib),
                              '--extern','os32_font='+str(font_rlib),'-L','dependency='+str(deps),'-o',str(unit/'t')],
                             capture_output=True,text=True,env=env)
            assert c.returncode==0,c.stderr  # a compile failure never counts as RED
            return subprocess.run([str(unit/'t'),'--test-threads=1','--nocapture'],capture_output=True,text=True,env=env)
        r=run();print(r.stdout,end='');assert r.returncode==0,r.stdout+r.stderr
        if mutate:
            for m in FONT_MUTATIONS:
                r=run(m);assert r.returncode==101 and m[4] in r.stdout,(m[0],r.stdout,r.stderr)
                print('RED '+m[0]+' rc=101 -> '+m[4])
            print(f'PASS {len(FONT_MUTATIONS)}/{len(FONT_MUTATIONS)} font.rs mutants')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mutate',action='store_true');args=p.parse_args()
    texts={n:(ROOT/n).read_text() for n in (APP,STUB,ALLOC)}
    with tempfile.TemporaryDirectory(prefix='g2s-gui-trim-') as d:
        work=Path(d);rlib=os32api_host.build(work)
        def run(m=None):
            src=dict(texts)
            if m:
                name,path,old,new,_=m
                assert src[path].count(old)==1,name
                src[path]=src[path].replace(old,new)
            app=src[APP].split('pub fn run_vt(',1)[1].split('\n/* ===',1)[0]
            stub=src[STUB].split('struct HookCell',1)[1].split('\n/* ===',1)[0]
            alloc=src[ALLOC].split('// OS32 has one cooperative task',1)[1].split('#[global_allocator]',1)[0]
            mocks=(ROOT/'tools/tests/rust_alloc_host.rs').read_text().split('#[test]',1)[0]
            code='#![allow(dead_code, unused_imports, static_mut_refs)]\nuse os32api::api;\n'
            code+='// OS32 has one cooperative task'+alloc+mocks+'\npub fn run_vt('+app+'\nstruct HookCell'+stub.replace('os32api::gui::','')
            init='pub fn init('+src[STUB].split('pub fn init(',1)[1].split('\nstruct HookCell',1)[0]
            code+=(ROOT/'tools/tests/gui_trim_host.rs').read_text().replace('// INSERT_PRODUCTION_INIT',init)
            path=work/'trim.rs';path.write_text(code)
            c=subprocess.run(['rustc','--edition=2021','--test',str(path),'--extern','os32api='+str(rlib),'-o',str(work/'trim')],capture_output=True,text=True)
            assert c.returncode==0,c.stderr
            return subprocess.run([str(work/'trim'),'--test-threads=1'],capture_output=True,text=True)
        r=run();print(r.stdout,end='');assert r.returncode==0,r.stdout+r.stderr
        if args.mutate:
            for m in MUTATIONS:
                r=run(m);assert r.returncode==101 and m[4] in r.stdout,(m[0],r.stdout,r.stderr)
                print('RED '+m[0]+' rc=101 -> '+m[4])
    font_phase(args.mutate)
if __name__=='__main__':main()
