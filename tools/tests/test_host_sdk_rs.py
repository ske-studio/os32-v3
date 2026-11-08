"""Run the real Rust Host Services SDK against generated KAPI slots."""
CHECK = dict(name='host-sdk-rs-host', tier=1,
    covers=['sdk/rust/os32api/src/host.rs', 'sdk/rust/os32api/src/kapi_generated.rs',
            'sdk/rust/os32api/src/lib.rs', 'sdk/rust/os32api/src/gui/**',
            'sdk/rust/os32api/src/cfg.rs', 'sdk/rust/os32api/src/ls.rs',
            'tools/tests/os32api_host.py', 'tools/tests/host_sdk_rs_host.rs',
            'tools/tests/test_host_sdk_rs.py'], needs=[], budget=3.5)
import argparse
from pathlib import Path
import subprocess
import tempfile
import os32api_host
ROOT = Path(__file__).resolve().parents[2]
MUTANTS = [
    ('LIST growth', 'if n != NOSPC { break n; }', 'if true { break n; }'),
    ('whole deadline', 'now.wrapping_sub(self.start) >= self.ticks', 'false'),
    ('host silence', 'now.wrapping_sub(self.progress) >= 100', 'false'),
    ('nonblocking', 'if self.nonblocking { return rc; }', 'if false { return rc; }'),
    ('close', 'let closed = (a.host_close)(h);', 'let closed = 0;'),
    ('capacity', 'if length as usize > output.len() { return NOSPC; }', 'if length as usize > output.len() { return 0; }'),
    ('cache generation', '(r.generation, id, output.as_mut_ptr())', '(0, id, output.as_mut_ptr())'),
    ('cache serial', 'r.serial) != Some(serial)', 'r.serial) != Some(0)'),
    ('stale refresh', 'if rc != STALE { return rc; }', 'if true { return rc; }'),
]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--mutate',action='store_true');args=parser.parse_args()
    source=(ROOT/'sdk/rust/os32api/src/host.rs').read_text()
    with tempfile.TemporaryDirectory(prefix='host-sdk-rs-') as folder:
        tmp=Path(folder);lib=os32api_host.build(tmp)
        (tmp/'main.rs').write_bytes((ROOT/'tools/tests/host_sdk_rs_host.rs').read_bytes())
        for mutant in [None]+(MUTANTS if args.mutate else []):
            code=source
            if mutant:
                name,old,new=mutant;assert code.count(old)==1,(name,code.count(old));code=code.replace(old,new)
            (tmp/'host.rs').write_text(code)
            build=subprocess.run(['rustc','--edition=2021','--test',str(tmp/'main.rs'),
                '--extern','os32api='+str(lib),'-o',str(tmp/'test')],capture_output=True,text=True)
            assert build.returncode==0,build.stderr
            run=subprocess.run([str(tmp/'test'),'--test-threads=1'],capture_output=True,text=True,timeout=30)
            assert (run.returncode==101) if mutant else (run.returncode==0),run.stdout+run.stderr
            print('RED runtime: '+mutant[0] if mutant else 'PASS Rust SDK runtime',flush=True)
if __name__=='__main__':main()


def run(ctx) -> int:
    argv = ['python3', '-B', 'tools/tests/test_host_sdk_rs.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, close_fds=False)
