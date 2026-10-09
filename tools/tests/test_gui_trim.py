"""g2s: execute the production run_vt, app hook trampoline and allocator."""
import argparse
from pathlib import Path
import subprocess
import tempfile
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
]
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
                print('RED '+m[0])
if __name__=='__main__':main()
