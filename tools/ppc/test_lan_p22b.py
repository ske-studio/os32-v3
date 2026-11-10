#!/usr/bin/env python3
"""Check byte probe instructions in a cross-assembled object."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
CHECK = dict(name='lan-p22b', tier=2, budget=60, covers=['arch/ppc/kernel_probe.S', 'tools/ppc/test_lan_p22b.py'], needs=[])
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT/'arch/ppc/kernel_probe.S'
CROSS = os.environ.get('CROSS_DIR','/home/hight/opt/cross-ppc')+'/bin/powerpc-elf-'

def check(source):
    with tempfile.TemporaryDirectory() as d:
        p=Path(d); (p/'probe.S').write_text(source)
        subprocess.run([CROSS+'gcc','-c',str(p/'probe.S'),'-o',str(p/'probe.o')],check=True)
        dis=subprocess.check_output([CROSS+'objdump','-d',str(p/'probe.o')],text=True)
        syms=subprocess.check_output([CROSS+'nm','-n',str(p/'probe.o')],text=True)
        labels={m.group(2):int(m.group(1),16) for m in re.finditer(r'^([0-9a-f]+)\s+\w\s+(\w+)$',syms,re.M)}
        ins={int(m.group(1),16):re.sub(r'\s+', '', m.group(2)) for m in re.finditer(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+){4}\s*([^\n]+)',dis,re.M)}
        start=labels['ppc_probe_copy8']; fault=labels['ppc_probe_copy8_insn']; resume=labels['ppc_probe_copy8_resume']; end=labels['ppc_probe_exec']
        return start==fault and [ins.get(i) for i in range(start,resume,4)]==['lbzr5,0(r3)','stbr5,0(r4)','lir3,0','blr'] and [ins.get(i) for i in range(resume,end,4)]==['lir3,1','blr']

def main():
    p=argparse.ArgumentParser(); p.add_argument('--mutate',action='store_true'); args=p.parse_args(); s=SOURCE.read_text(); assert check(s), 'byte probe instruction sequence'
    if args.mutate:
        for old,new in [('    lbz 5,0(3)','    lbz 5,1(3)'),('    stb 5,0(4)','    stb 5,0(3)'),('    stb 5,0(4)\n    li 3,0','    stb 5,0(4)\n    li 3,1'),('    lbz 5,0(3)','    lwz 6,0(3)\n    lbz 5,0(3)'),('ppc_probe_copy8_resume:\n    li 3,1','ppc_probe_copy8_resume:\n    stb 5,0(4)\n    li 3,1')]:
            assert s.count(old)==1 and not check(s.replace(old,new)), 'expected instruction sequence failure'
            print('RED instruction sequence:',old.splitlines()[0])
    print('p22b PASS')
if __name__=='__main__': main()
