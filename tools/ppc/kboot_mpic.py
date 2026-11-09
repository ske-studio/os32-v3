#!/usr/bin/env python3
"""E2 product MPIC: recorded bus, runtime mutations, real mac99 IPI delivery."""
import argparse
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
import kboot_vectors as v
from run import command, require
CHECK = dict(name='kboot-mpic', tier=2, budget=600, needs=[],
             covers=v.CHECK['covers']+['platform/powermac/mpic*',
                     'platform/powermac/kboot_capture.c', 'platform/powermac/kboot_info.h',
                     'platform/powermac/ppc_pmap.h', 'tools/ppc/kboot_mpic*'])
# Expected exit and reach marker are both mandatory; errors are not RED.
MUTATIONS = [
    ('tpr', 'write_reg(TPR, 0);', 'write_reg(TPR, 15);', 13),
    ('eoi', 'active = 0;\n    write_reg(EOI, 0);', 'active = 0; /* omitted EOI */', 21),
    ('spurious', 'if (token == PM_MPIC_SPURIOUS) return;',
     'if (token == PM_MPIC_SPURIOUS) { write_reg(EOI, 0); return; }', 20),
    ('ipi-mask', 'write_reg(IPI_VECTOR, PRIORITY_ONE | PM_MPIC_IPI_VECTOR);',
     'write_reg(IPI_VECTOR, MASK | PRIORITY_ONE | PM_MPIC_IPI_VECTOR);', 12),
    ('endian', 'mmio_order == PM_MPIC_MMIO_LE', 'mmio_order == PM_MPIC_MMIO_BE', 10),
]

def host(out, source, expected=0):
    out.mkdir(parents=True, exist_ok=True)
    command(['gcc','-m32','-ffreestanding','-fno-pie','-no-pie','-nostdlib','-static',
             '-std=gnu11','-O2','-Wall','-Wextra','-Werror','-DPM_MPIC_HOST_TEST',
             *['-I'+str(v.ROOT/p) for p in ('arch/ppc','include','platform/powermac')],
             source,v.ROOT/'tools/ppc/kboot_mpic_host.c','-o',out/'host'])
    p=subprocess.run(['qemu-i386',out/'host'],capture_output=True,timeout=10)
    (out/'host.log').write_bytes(p.stdout)
    require(p.returncode==expected and p.stdout==b'CIPM'+expected.to_bytes(4,'little'),
            'host expected rc/mark '+str(expected)+' actual '+str(p.returncode)+' '+repr(p.stdout))
    print('host '+str(expected)+' PASS',flush=True)


def image(out, source):
    out.mkdir(parents=True,exist_ok=True)
    text=(v.ROOT/'tools/ppc/kboot_vectors_fixture.c').read_text()
    text=text.replace('#include "ppc_trap.h"','#include "ppc_trap.h"\n#include "mpic.h"')
    text=text.replace('static struct pm_escc serial;', '''static struct pm_escc serial;
static u32 mpic_pa, mpic_bytes;
static int mpic_order;
static volatile u32 ipis;
''')
    a=text.index('void product_trap_dispatch('); b=text.index('void fixture_setup_real(',a)
    text=text[:a]+'''
void ppc_trap_fatal_sink(const struct ppc_frame *f,u32 reason)
{ (void)f; (void)reason; bad(); }
static void ipi(struct ppc_frame *f)
{
    if (f->vector!=0x500 || (msr()&0x8030)!=0x30) bad();
    ++ipis;
}
static u32 tb(void) { u32 v; __asm__ volatile("mftb %0":"=r"(v)); return v; }
void fixture_main(void)
{
    say("E2-REACHED\\r\\n");
    if (pm_mpic_init(0xfe340000+(mpic_pa&4095),mpic_bytes,mpic_order)) {
        say("INIT-FAIL\\r\\n"); fixture_finish();
    }
    say("INIT-PASS\\r\\n");
    pm_mpic_set_ipi_handler(ipi);
    u32 enabled=msr()|0x8000;
    __asm__ volatile("mtmsr %0; isync" : : "r"(enabled) : "memory");
    for (u32 n=1;n<=2;++n) {
        if (!pm_mpic_send_self_ipi(0)) bad();
        u32 start=tb();
        while (ipis<n && tb()-start<10000000U) __asm__ volatile("" ::: "memory");
        if (ipis!=n) {
            say(n==1 ? "IPI1-FAIL\\r\\n" : "IPI2-FAIL\\r\\n"); fixture_finish();
        }
        say(n==1 ? "IPI1-PASS\\r\\n" : "IPI2-PASS\\r\\n");
    }
    say("E2-DELIVERY-PASS\\r\\n"); fixture_finish();
}
''' +text[b:]
    text=text.replace('    page[0]=0x76543210;', '''    for (u32 n=0;n<((mpic_pa&4095)+mpic_bytes+4095)/4096;++n)
        io_pt[((0xfe340000U>>12)&1023)+n]=((mpic_pa&~4095U)+4096*n)|0x13;
    page[0]=0x76543210;''')
    text=text.replace('    ppc_disable();', '''    const struct pm_kboot_info *info=pm_kboot_capture(ci);
    int resolved=pm_mpic_resolve(info,&mpic_pa,&mpic_bytes,&mpic_order);
    if (resolved) {
        char code[3]={(char)('0'+resolved), '\\r',0};
        say("RESOLVE-FAIL "); say(code); ppc_stop();
    }
    ppc_disable();''')
    fixture=out/'fixture.c'; fixture.write_text(text)
    old=v.PRODUCT
    v.PRODUCT=[p for p in old if p!='tools/ppc/kboot_vectors_fixture.c']+[
        str(fixture),str(source),'platform/powermac/mpic_tree.c','platform/powermac/kboot_capture.c']
    try: return v.build(out,v.ROOT/'arch/ppc/kernel_vectors.S')
    finally: v.PRODUCT=old


def observe(out, elf, markers):
    # Keep D1's bounded QMP/PC observer and substitute E2 terminal criteria.
    import inspect
    body=inspect.getsource(v.observe)
    start=body.index('            if red is not None:')
    end=body.index("            q.call('quit')",start)
    body=body[:start]+'''            require(last==syms['fixture_done'],'unexpected E2 stop '+hex(last or 0)+' '+repr(serial))
            for marker in markers:
                require(serial.count(marker)==1,'E2 missing mark '+repr(marker)+' '+repr(serial))
''' +body[end:]
    body=body.replace('QEMU canary/retry/dispatch/nested spill PASS','QEMU terminal PC PASS')
    scope=vars(v).copy(); scope['markers']=markers
    exec(body,scope); scope['observe'](out,elf)
    print('mac99 E2 '+repr(markers)+' PASS',flush=True)


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--mutate',action='store_true'); args=parser.parse_args()
    out=v.ROOT/'build/out/ppc/kboot-mpic'; source=v.ROOT/'platform/powermac/mpic.c'
    host(out/'host',source)
    dest=out/'qemu'; elf=image(dest,source)
    header=command([str(v.CROSS)+'readelf','-hSWl',elf])
    (dest/'readelf.log').write_text(header)
    dis=command([str(v.CROSS)+'objdump','-d',elf])
    (dest/'objdump.log').write_text(dis)
    require('PowerPC' in header and 'big endian' in header,'ELF32 PPC BE')
    require('lwbrx' in dis and 'stwbrx' in dis and 'eieio' in dis,'ordered LE bus instructions')
    require(not command([str(v.CROSS)+'nm','-u',elf]).strip(),'undefined symbols')
    observe(dest,elf,[b'E2-REACHED',b'INIT-PASS',b'IPI1-PASS',b'IPI2-PASS',b'E2-DELIVERY-PASS'])
    if args.mutate:
        original=source.read_text()
        for name,old,new,expected in MUTATIONS:
            require(original.count(old)==1,'unique anchor '+name)
            dest=out/name; dest.mkdir(parents=True,exist_ok=True)
            mutant=dest/'mpic.c'; mutant.write_text(original.replace(old,new))
            host(dest,mutant,expected)
            if name!='spurious':
                qout=dest/'qemu'; elf=image(qout,mutant)
                marks=[b'E2-REACHED',b'INIT-FAIL'] if name=='endian' else [b'E2-REACHED',b'INIT-PASS']
                if name!='endian':
                    marks += [b'IPI1-PASS',b'IPI2-FAIL'] if name=='eoi' else [b'IPI1-FAIL']
                observe(qout,elf,marks)
            (dest/'RED.log').write_text('expected/actual host rc and reach: '+str(expected)+'; QEMU fixture_done with '+repr(marks if name!='spurious' else []))
            print(name+': runtime RED',flush=True)
    print('kboot-mpic PASS',flush=True)
if __name__=='__main__': main()
