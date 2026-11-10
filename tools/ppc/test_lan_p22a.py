#!/usr/bin/env python3
"""Verify the byte probe's public declarations."""
import argparse
from pathlib import Path
import subprocess
import tempfile
CHECK = dict(name='lan-p22a', tier=2, budget=60, covers=['arch/ppc/kernel_probe.h', 'tools/ppc/test_lan_p22a.py'], needs=[])
ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'arch/ppc/kernel_probe.h'

def check(header):
    with tempfile.TemporaryDirectory() as d:
        p = Path(d)
        (p/'kernel_probe.h').write_text(header)
        (p/'test.c').write_text('#include "kernel_probe.h"\nint (*f)(u32, u8 *) = ppc_probe_copy8;\nconst char *a = ppc_probe_copy8_insn, *b = ppc_probe_copy8_resume;\n')
        return subprocess.run(['gcc','-Werror','-fsyntax-only','-I'+d,'-I'+str(ROOT/'include'),str(p/'test.c')], capture_output=True).returncode

def main():
    a=argparse.ArgumentParser(); a.add_argument('--mutate',action='store_true'); args=a.parse_args()
    s=HEADER.read_text(); assert check(s)==0
    if args.mutate:
        for old,new in [('ppc_probe_copy8(u32 va, u8 *out)','ppc_probe_copy8(u32 va, u32 *out)'),('ppc_probe_copy8_insn[]','ppc_probe_copy8_insnx[]')]:
            assert s.count(old)==1 and check(s.replace(old,new)) != 0
            print('RED declaration:',old)
    print('p22a PASS')
if __name__ == '__main__': main()
