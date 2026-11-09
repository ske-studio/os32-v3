#!/usr/bin/env python3
"""H5: acceptance CPU provider, real product gates and record trace."""
CHECK = dict(name='sep5-accept-cpu-host', tier=1, needs=[], budget=5.0,
    covers=['arch/x86/video/gf_accept_cpu.c', 'arch/ppc/video/gf_accept_cpu.c',
            'include/video_wc.h', 'tools/tests/test_sep5_accept_cpu.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'mm/paging.h', 'build/geforce-module.mk'])
import sys
import tempfile
from pathlib import Path
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = {
    'vendor': ('if (!intel) return 0;', 'if (0) return 0;'),
    'capability': ('if (!(features & (1U << 5)))', 'if (0)'),
    'flags': ('set_flags(saved);', '(void)saved;'),
    'phase': ('cache_begin(log, "msr"); cache_number(log, phase);', 'cache_begin(log, "msr"); cache_number(log, phase ^ 1U);'),
}
HARNESS = r'''
#include "types.h"
static u32 state, scenario, reads, calls;
static u32 flags(void) { return state; }
static void set_flags(u32 v) { if (scenario != 1) state=v; }
static void cpuid(u32 leaf,u32 sub,u32 *a,u32 *b,u32 *c,u32 *d) {
    (void)sub; ++calls; *a=*b=*c=*d=0;
    if (!leaf) { *a=1; *b=scenario==2 ? 0 : 0x756e6547; *c=0x6c65746e; *d=0x49656e69; }
    if (leaf==1) *d=scenario==3 ? (1U<<12) : (1U<<5)|(1U<<12);
    if (leaf==0x80000000) *a=0x80000000;
}
static unsigned long long msr(u32 reg) { ++reads; return reg==0x2ff ? 0x800 : 0; }
#include "cpu.inc"
u32 paging_pte_raw(u32 addr) { return addr|PTE_PRESENT|PAGE_RW|PTE_PCD; }
static char trace[4096]; static u32 used;
static void report(const char *s) { while (*s) trace[used++]=*s++; }
static int equal(const char *s) { u32 i=0; while(s[i] && trace[i]==s[i]) ++i; return !s[i] && i==used; }
static void finish(int rc) { __asm__ volatile("int $0x80"::"a"(1),"b"(rc):"memory"); for (;;) {} }
void _start(void) {
    for (scenario=0;scenario<4;++scenario) {
        state=0x202; reads=calls=used=0;
        int rc=arch_video_cache_inspect(1,0x30000000,4096,report);
        if (state!=0x202 || rc!=(scenario==2 ? 0 : 1)) finish(1);
        if ((scenario && reads) || (!scenario && reads!=2) || (scenario==1 && calls)) finish(1);
        if (!scenario && !equal("GFA1 fixture byte-uc pte 1 0 805306387 \n"
            "GFA1 fixture byte-uc cpu 0 0 1 1970169159 1818588270 1231384169 \n"
            "GFA1 fixture byte-uc cpu 1 0 0 0 0 4128 \n"
            "GFA1 fixture byte-uc msr 1 254 0 0 \n"
            "GFA1 fixture byte-uc msr 1 767 2048 0 \n"
            "GFA1 fixture byte-uc cpu 2147483648 0 2147483648 0 0 0 \n")) finish(1);
    }
    finish(0);
}
'''
def exercise(mutation=None):
    source=(ROOT/'arch/x86/video/gf_accept_cpu.c').read_text()
    if mutation:
        old,new=MUTATIONS[mutation]
        assert source.count(old)==1, (mutation,source.count(old))
        source=source.replace(old,new)
    a=source.index('/* Read-only CPU primitives begin.')
    b=source.index('/* Read-only CPU primitives end. */')
    source=source[:a]+source[b:]
    with tempfile.TemporaryDirectory(prefix='accept-cpu-') as td:
        p=Path(td); (p/'cpu.inc').write_text(source); (p/'host.c').write_text(HARNESS)
        host32.build(['gcc','-m32','-std=gnu11','-O2','-ffreestanding','-fno-pie',
            '-fno-stack-protector','-nostdlib','-static','-no-pie', '-DGF_ACCEPT_MODE=0',
            '-DGF_ACCEPT_ID="fixture"',*include_flags(ROOT),'-I'+str(ROOT),str(p/'host.c'),'-o',str(p/'host')],check=True)
        result=host32.run([str(p/'host')],capture_output=True,timeout=60)
        assert result.returncode==(1 if mutation else 0),(mutation,result.returncode)
    print('RED '+mutation if mutation else 'PASS CPU gates, flags restoration, exact phase/record trace')
if __name__=='__main__':
    exercise()
    if '--mutate' in sys.argv:
        for mutation in MUTATIONS: exercise(mutation)
def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3','-B',__file__]+(['--mutate'] if ctx.mutate else []),
        cwd=ctx.root,env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)))
