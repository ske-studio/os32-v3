#!/usr/bin/env python3
"""Verify the diagnostic onfault declaration."""
import argparse
from pathlib import Path
import subprocess
import tempfile
CHECK = dict(name='lan-p22c', tier=2, budget=60, covers=['arch/ppc/ppc_trap.h', 'tools/ppc/test_lan_p22c.py'], needs=[])
ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT/'arch/ppc/ppc_trap.h'
def check(s):
    with tempfile.TemporaryDirectory() as d:
        p=Path(d); (p/'ppc_trap.h').write_text(s)
        (p/'test.c').write_text('#include "ppc_trap.h"\nint (*f)(u32, u32, u32) = ppc_trap_onfault_data;\n')
        return subprocess.run(['gcc','-m32','-Werror','-fsyntax-only','-I'+d,'-I'+str(ROOT/'arch/ppc'),'-I'+str(ROOT/'include'),str(p/'test.c')],capture_output=True).returncode

def main():
    p=argparse.ArgumentParser(); p.add_argument('--mutate',action='store_true'); args=p.parse_args(); s=HEADER.read_text(); assert check(s)==0
    if args.mutate:
        for old,new in [('ppc_trap_onfault_data(u32 pc, u32 va, u32 resume)','ppc_trap_onfault_data(u32 pc, u32 va)'),('ppc_trap_onfault_data','ppc_trap_onfault_datax')]:
            assert s.count(old)==1 and check(s.replace(old,new))!=0
            print('RED declaration:',old)
    print('p22c PASS')
if __name__=='__main__': main()
