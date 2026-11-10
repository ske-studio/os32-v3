#!/usr/bin/env python3
"""Exercise diagnostic onfault using the product trap dispatcher."""
import argparse
from pathlib import Path
import subprocess
import tempfile
CHECK = dict(name='lan-p22d', tier=2, budget=120, covers=['arch/ppc/kernel_trap.c', 'tools/ppc/test_lan_p22d*'], needs=[])
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'arch/ppc/kernel_trap.c'
def check(s, expected=0):
    with tempfile.TemporaryDirectory() as d:
        p=Path(d); (p/'trap.c').write_text(s)
        subprocess.run(['gcc','-m32','-ffreestanding','-fno-pie','-no-pie','-nostdlib','-static','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-DPPC_TRAP_HOST_TEST','-I'+str(ROOT/'arch/ppc'),'-I'+str(ROOT/'include'),'-I'+str(ROOT),str(p/'trap.c'),str(ROOT/'arch/ppc/pd_walk.c'),str(ROOT/'tools/ppc/test_lan_p22d_host.c'),'-o',str(p/'host')],check=True)
        result=subprocess.run(['qemu-i386',str(p/'host')],capture_output=True)
        assert result.returncode==expected, (expected,result.returncode,result.stdout)
        if expected: assert result.stdout[-4:]==expected.to_bytes(4,'little'), 'missing reach marker'

def main():
    p=argparse.ArgumentParser(); p.add_argument('--mutate',action='store_true'); args=p.parse_args(); s=SOURCE.read_text(); check(s)
    if args.mutate:
        changes=[('isi','reason == PPC_TRAP_DSI_IO) :','reason == PPC_TRAP_DSI_IO || reason == PPC_TRAP_ISI) :',20),('va','(f->vector == PPC_VECTOR_ISI ? f->srr0 : f->dar) == probe.va &&','1 &&',23),('consume','probe.active = 0;\n            f->srr0 = probe.resume;','f->srr0 = probe.resume;',11),('occupied','if (probe.active || ((pc | resume) & 3)) return 0;','if ((pc | resume) & 3) return 0;',24)]
        for name,old,new,code in changes:
            assert s.count(old)==1, name
            check(s.replace(old,new),code); print(name+': runtime RED with reach marker')
    print('p22d PASS')
if __name__=='__main__': main()
