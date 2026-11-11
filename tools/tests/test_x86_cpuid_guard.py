#!/usr/bin/env python3
"""Model a CPU whose ID bit is immutable; run the real probe separately too."""
CHECK = dict(name='x86-cpuid-guard-host', tier=1, needs=[], budget=5,
    covers=['arch/x86/arch_cpu.h', 'arch/x86/x86_flags.h',
            'tools/tests/test_x86_cpuid_guard.py', 'tools/tests/host32.py'])
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, header, model):
    prefix = '''typedef unsigned long u32;
static struct { u32 cr3; } kernel_tss;
'''
    if model:
        prefix += 'static u32 flags, original, mutable_id, cpuid_calls;\n'
        prefix += '''static void probe_flags(u32 probe, u32 *changed, u32 old) {
 flags = mutable_id ? probe : flags;
 *changed = flags; flags = old;
}
'''
        header, n = re.subn(r'__asm__ volatile\("pushfl; popl %0" : "=r"\(old\)\);', 'old = flags;', header)
        assert n == 1
        header, n = re.subn(r'__asm__ volatile\("pushl %0; popfl; pushfl; popl %1; pushl %2; popfl".*?;',
                           'probe_flags(probe, &changed, old);', header, flags=re.S)
        assert n == 1
        header, n = re.subn(r'__asm__ volatile\("cpuid".*?;',
                           'cpuid_calls++; d = (1UL << 23); a = 1; b = c = 0;', header)
        assert n == 3
        test = '''for (unsigned id=0; id<2; id++) {
 original=flags=0x202 | (id<<21); mutable_id=0; cpuid_calls=0;
 if (arch_cpu_has_mmx() || flags!=original) return 1;
 arch_cpu_serialize(); if (cpuid_calls || flags!=original) return 2;
 mutable_id=1;
 if (!arch_cpu_has_mmx() || cpuid_calls!=2 || flags!=original) return 3;
 arch_cpu_serialize(); if (cpuid_calls!=3 || flags!=original) return 4;
}'''
    else:
        test = '''u32 before, after;
 __asm__ volatile("pushfl; popl %0":"=r"(before));
 if (!arch_cpu_has_cpuid()) return 5;
 __asm__ volatile("pushfl; popl %0":"=r"(after));
 if ((before^after)&(1UL<<21)) return 6;
 arch_cpu_serialize(); (void)arch_cpu_has_mmx();'''
    # MMU functions are unused; GDT/TSS substitutes isolate just the CPU helpers.
    header = header.replace('#include "gdt.h"', '// #include "gdt.h"').replace('#include "tss.h"', '// #include "tss.h"')
    code = prefix + header + '\nstatic int test(void) {\n'+test+'\nreturn 0; }\n'
    code += 'void _start(void) { int rc=test(); __asm__ volatile("int $0x80"::"a"(1),"b"(rc)); __builtin_unreachable(); }\n'
    source=work/'probe.c'; source.write_text(code); exe=work/'probe'
    host32.build(['gcc','-m32','-std=gnu11','-O2','-ffreestanding','-fno-pie','-no-pie',
                  '-nostdlib','-static','-I'+str(ROOT/'arch/x86'), str(source),'-o',str(exe)],check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--mutate',action='store_true');args=parser.parse_args()
    header=(ROOT/'arch/x86/arch_cpu.h').read_text()
    with tempfile.TemporaryDirectory() as td:
        work=Path(td)
        assert observe(work,header,False)==0
        assert observe(work,header,True)==0
        if args.mutate:
            for anchor in ('if (!arch_cpu_has_cpuid()) return;', 'if (!arch_cpu_has_cpuid()) return 0;'):
                assert header.count(anchor)==1
                rc=observe(work,header.replace(anchor,''),True)
                assert rc in (1,2),rc
                print('RED runtime missing CPUID guard rc='+str(rc))
    print('PASS immutable/mutable ID, flag restoration, serialize/MMX, real CPU probe')


if __name__=='__main__':
    main()


def run(ctx):
    return subprocess.call(['python3','-B',__file__,*(['--mutate'] if ctx.mutate else [])],cwd=ctx.root)
