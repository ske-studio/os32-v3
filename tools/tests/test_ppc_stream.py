"""Freeze >4KiB cooperative draining and diagnostic mirror suppression."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
SOURCES = {'stream.inc':'platform/powermac/console_stream.c',
           'log.inc':'platform/powermac/console_log.c'}
MUTANTS = [('poll removed','stream.inc','    pm_terminal_poll();',''),
           ('diagnostic mirrored','log.inc','    boot_serial_mirror = rshell_active = 0;','')]
def run(tmp, sources):
    for name,text in sources.items(): (tmp/name).write_text(text)
    exe=tmp/'stream'
    subprocess.run(['gcc','-m32','-std=gnu11','-ffreestanding','-fno-builtin',
        '-fno-pie','-no-pie','-fno-stack-protector','-nostdlib','-static',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-D__KERNEL_BUILD__','-I'+str(tmp),'-I'+str(ROOT),
        '-I'+str(ROOT/'platform/powermac'),'-I'+str(ROOT/'arch/ppc'),*include_flags(ROOT),
        str(ROOT/'tools/tests/ppc_stream_host.c'),
        str(ROOT/'platform/powermac/lan_terminal.c'),str(ROOT/'lib/kstring_c.c'),
        '-o',str(exe)],check=True)
    return host32.run([str(exe)],runner='qemu').returncode

def main():
    p=argparse.ArgumentParser();p.add_argument('--mutate',action='store_true');a=p.parse_args()
    sources={n:(ROOT/f).read_text() for n,f in SOURCES.items()}
    with tempfile.TemporaryDirectory(prefix='ppc-stream-') as d:
        tmp=Path(d)
        subprocess.run(['python3','-B','sdk/gen_kapi.py','--target','ppc-powermac',
                        '--out-dir',str(tmp/'gen')],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
        generated=(tmp/'gen/kapi/kapi_generated.c').read_text()
        start=generated.index('void __cdecl wrap_tvram_clear(void)')
        end=generated.index('\n}',start)+2
        (tmp/'clear.inc').write_text(generated[start:end])
        assert run(tmp,sources)==0
        print('PASS PPC stream: 12288 bytes, no drops, diagnostic once, no recursion, clear')
        if a.mutate:
            for label,name,old,new in MUTANTS:
                assert sources[name].count(old)==1
                mutated=dict(sources);mutated[name]=sources[name].replace(old,new)
                assert run(tmp,mutated)==1,label
                print('RED runtime: '+label)
if __name__=='__main__': main()
