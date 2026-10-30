"""f8: execute the real Os32Alloc against mem_alloc/free mocks, without newlib.
段: T2f f8
"""
CHECK = dict(name='rust-alloc-host', tier=1,
    covers=['sdk/rust/os32api/src/lib.rs', 'tools/tests/rust_alloc_host.rs', 'tools/tests/os32api_host.py', 'tools/check_map.d/check-rust-alloc-host.yaml', 'build/checks.d/check-rust-alloc-host.mk'],
    needs=[], budget=1.871332)

import argparse
from pathlib import Path
import subprocess
import tempfile
import os32api_host

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'sdk/rust/os32api/src/lib.rs'
MUTATIONS = [
    ('u32::try_from(layout.size())', 'Ok::<u32, ()>(layout.size() as u32)', 'overflow must not call mem_alloc'),
    ('let align = layout.align().max(core::mem::align_of::<AllocPrefix>());',
     'let align = core::mem::align_of::<AllocPrefix>();', 'Layout alignment'),
    ('(api().mem_free)((*prefix).base);', '(api().mem_free)(ptr);', 'dealloc must use raw base'),
    ('Some(n) => n, None => return core::ptr::null_mut(),',
     'Some(n) => n, None => 8,', 'overflow must not call mem_alloc'),
    ('if !next.is_null() {\n            core::ptr::copy_nonoverlapping',
     'if next.is_null() { self.dealloc(ptr, layout); }\n        if !next.is_null() {\n            core::ptr::copy_nonoverlapping',
     'failed realloc retains old block'),
    ('ptr.write_bytes(0, layout.size());', 'ptr.write_bytes(1, layout.size());', 'explicit zeroing'),
    ('if base.is_null() && GUI_RETRY.0.get() && !IN_TRIM.0.get() {',
     'while base.is_null() && GUI_RETRY.0.get() && !IN_TRIM.0.get() {', 'retry-unbounded'),
    ('GUI_RETRY.0.get() && !IN_TRIM.0.get()', '!IN_TRIM.0.get()', 'retry-without-gui'),
    ('GUI_RETRY.0.get() && !IN_TRIM.0.get()', 'GUI_RETRY.0.get()', 'retry-in-trim'),
    ('if layout.size() == 0 { return core::ptr::null_mut(); }',
     'if layout.size() == 0 { (api().sys_yield)(); return core::ptr::null_mut(); }', 'retry-size0'),

]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate',action='store_true')
    args=parser.parse_args()
    source=SOURCE.read_text()
    source=source[source.index('// OS32 has one cooperative task'):source.index('#[global_allocator]')]
    fixture=(ROOT/'tools/tests/rust_alloc_host.rs').read_text()
    with tempfile.TemporaryDirectory(prefix='f8-rust-') as d:
        work=Path(d); rlib=os32api_host.build(work)
        def run(code):
            path=work/'alloc.rs'
            path.write_text('use os32api::api;\n'+code+fixture)
            result=subprocess.run(['rustc','--edition=2021','--test',str(path),'--extern','os32api='+str(rlib),'-o',str(work/'test')],capture_output=True,text=True)
            assert result.returncode==0,result.stderr
            return subprocess.run([str(work/'test'),'--test-threads=1'],capture_output=True,text=True)
        normal=run(source); print(normal.stdout,end=''); assert normal.returncode==0,normal.stderr
        if args.mutate:
            for old,new,expected in MUTATIONS:
                assert source.count(old)==1,old
                result=run(source.replace(old,new))
                assert result.returncode==101 and expected in result.stdout,(expected,result.stdout,result.stderr)
                print('RED '+expected+' rc=101 -> '+expected)
            print(f'PASS {len(MUTATIONS)} runtime mutants')
    return 0
if __name__=='__main__': raise SystemExit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_rust_alloc.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
