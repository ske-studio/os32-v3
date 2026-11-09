#!/usr/bin/env python3
"""D2 product dispatch, host classification and D1 mac99 fixture reuse."""
import argparse
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
import kboot_vectors as v
from run import command, require, CriterionFailure
CHECK = dict(name='kboot-trap',tier=2,budget=600,
             covers=v.CHECK['covers']+['arch/ppc/kernel_trap.c','tools/ppc/kboot_trap*'],needs=[])

def host(out, source):
    out.mkdir(parents=True,exist_ok=True)
    command(['gcc','-m32','-ffreestanding','-fno-pie','-no-pie','-nostdlib','-static','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
             '-DPPC_TRAP_HOST_TEST','-I'+str(v.ROOT/'arch/ppc'),
             '-I'+str(v.ROOT/'include'),'-I'+str(v.ROOT/'platform/powermac'),
             '-I'+str(v.ROOT),source,v.ROOT/'arch/ppc/pd_walk.c',
             v.ROOT/'tools/ppc/kboot_trap_host.c','-o',out/'host'])
    p=subprocess.run(['qemu-i386',out/'host'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/'host.log').write_bytes(p.stdout)
    require(p.returncode==0,'host runtime verdict '+str(p.returncode))

def image(out, source, case):
    out.mkdir(parents=True,exist_ok=True)
    text=(v.ROOT/'tools/ppc/kboot_vectors_fixture.c').read_text()
    text=text.replace('#include "ppc_pd.h"','#include "ppc_pd.h"\n#include "ppc_trap.h"')
    a=text.index('void ppc_trap_dispatch('); b=text.index('void fixture_setup_real(',a)
    text=text[:a]+'''
static u32 table_read(u32 pa, void *ctx)
{ (void)ctx; return *(volatile u32 *)pa; }
static void hex(u32 n)
{ char s[9]; for(unsigned i=0;i<8;++i) s[i]="0123456789abcdef"[(n>>(28-4*i))&15]; s[8]=0; say(s); }
void ppc_trap_fatal_sink(const struct ppc_frame *f,u32 reason)
{
    if ((msr()&0x8030)!=0x30 || serial.csr<IO_VA) ppc_stop();
    say("DUMP "); hex(reason); say(" PC "); hex(f->srr0);
    say(" VA "); hex(f->dar); say(" DSISR "); hex(f->dsisr); say("\\r\\n");
}
void fixture_main(void)
{
    ppc_trap_set_reader(table_read,0);
    ppc_trap_set_fatal_ready(1);
#if TRAP_CASE == 2 || TRAP_CASE == 5
    if (!ppc_trap_onfault((u32)fixture_load+(TRAP_CASE==5?4:0),TEST_VA,PPC_TRAP_DSI_PROTECT,
                         (u32)fixture_resume)) bad();
    fixture_canary(0);
    for (u32 r=0;r<32;++r) {
        u32 expected=r==1 ? 0x00301000 : r==3 ? TEST_VA : 256+r;
        if (fixture_regs[r]!=expected) bad();
    }
    if (fixture_regs[32]!=0x1234 || fixture_regs[33]!=0x12345678 ||
        fixture_regs[34]!=0x2345 || fixture_regs[35]!=0xa0000000 ||
        (fixture_regs[36]&0x8030)!=0x30) bad();
    say("ONFAULT-PASS\\r\\n"); fixture_finish();
#elif TRAP_CASE == 3
    __asm__ volatile(".globl fixture_fault_pc\\nfixture_fault_pc: trap");
#elif TRAP_CASE == 4
    /* Enter the installed 0x500 vector with a real saved supervisor context. */
    __asm__ volatile("lis 3,fixture_fault_pc@ha; addi 3,3,fixture_fault_pc@l;"
                     "mtsrr0 3; li 3,0x32; mtsrr1 3; li 0,0; mtmsr 0; isync; ba 0x500;"
                     ".globl fixture_fault_pc\\nfixture_fault_pc: nop" : : : "r0", "r3");
#else
    fixture_canary(0);
#endif
    bad();
}
''' +text[b:]
    # Pin a PP0 translation, then set Ks=1 for segment 5: real read protection.
    needle='    ppc_cache_copy((void *)0,ppc_kernel_vectors.src,ppc_kernel_vectors.size);'
    text=text.replace(needle,'''    struct ppc_frame test={.vector=PPC_VECTOR_DSI,.dar=TEST_VA,.dsisr=PPC_DSISR_MISS};
    if (!fixture_hpt_spill(&test)) ppc_stop();
    u32 sr5=0x60000000|ppc_hpt_vsid(5);
    __asm__ volatile("mtsr 5,%0; isync" : : "r"(sr5) : "memory");
'''+needle+'''
#if TRAP_CASE == 6
    ppc_trap_set_fatal_ready(1);
    struct ppc_frame fatal={.vector=0x700};
    ppc_trap_dispatch(&fatal);
#endif
''')
    fixture=out/'fixture.c'; fixture.write_text(text)
    old_product,old_flags=v.PRODUCT,v.FLAGS
    v.PRODUCT=[p for p in old_product if p!='tools/ppc/kboot_vectors_fixture.c']+[str(fixture),str(source)]
    v.FLAGS=old_flags+['-DTRAP_CASE='+str(case)]
    try: return v.build(out,v.ROOT/'arch/ppc/kernel_vectors.S')
    finally: v.PRODUCT,v.FLAGS=old_product,old_flags

def observe(out,elf,case):
    # Reuse D1's bounded terminal observer, substituting D2 stop/serial criteria.
    import inspect
    body=inspect.getsource(v.observe)
    body=body.replace("'ppc_kernel_vector_stop']","'ppc_kernel_vector_stop','ppc_trap_stop']")
    start=body.index("            require(last==")
    end=body.index("            q.call('quit')",start)
    body=body[:start]+'''            expected = syms['fixture_done'] if case==2 else syms['ppc_trap_stop']
            require(last==expected,'D2 terminal PC '+hex(last or 0)+' '+repr(serial[-2000:]))
            if case==2:
                require(b'ONFAULT-PASS' in serial and b'DUMP' not in serial,'onfault continuation')
            elif case==6:
                require(b'DUMP' not in serial,'no real-mode sink')
            else:
                reason={1:3,3:0x106,4:9,5:3}[case]
                pc=syms['fixture_load' if case in (1,5) else 'fixture_fault_pc']
                marker=('DUMP %08x PC %08x'%(reason,pc)).encode()
                require(marker in serial,'fatal dump '+repr(serial[-2000:]))
                if case==1:
                    require(b'VA 50000000 DSISR 08000000' in serial,'protection syndrome')
''' +body[end:]
    body=body.replace('QEMU canary/retry/dispatch/nested spill PASS','QEMU D2 case '+str(case)+' PASS')
    scope=vars(v).copy(); scope['case']=case
    exec(body,scope)
    scope['observe'](out,elf)

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--mutate',action='store_true'); args=parser.parse_args()
    out=v.ROOT/'build/out/ppc/kboot-trap'; source=v.ROOT/'arch/ppc/kernel_trap.c'
    host(out/'host',source)
    for case in range(1,7):
        dest=out/str(case); elf=image(dest,source,case)
        (dest/'readelf.log').write_text(command([str(v.CROSS)+'readelf','-hSW',elf]))
        (dest/'objdump.log').write_text(command([str(v.CROSS)+'objdump','-d',elf]))
        observe(dest,elf,case)
    if args.mutate:
        changes=[('protection-return','reason = fault_reason(f);','reason = fault_reason(f); if (reason == PPC_TRAP_DSI_PROTECT) { f->srr0 += 4; return; }'),
                 ('onfault-pc','probe.active && f->srr0 == probe.pc &&','probe.active &&'),
                 ('unregistered-return','reason = PPC_TRAP_UNREGISTERED;','return;'),
                 ('real-mmio','f && translated && fatal_ready && ppc_root_current()','f && fatal_ready && ppc_root_current()'),
                 ('classification','cause == PPC_DSISR_PROTECT','cause == 0x00400000')]
        original=source.read_text()
        for name,old,new in changes:
            require(original.count(old)==1,'unique anchor '+name)
            dest=out/name; dest.mkdir(parents=True,exist_ok=True)
            mutant=dest/'kernel_trap.c'; mutant.write_text(original.replace(old,new))
            try: host(dest,mutant)
            except CriterionFailure as e:
                (dest/'RED.log').write_text(str(e)); print(name+': runtime RED',flush=True)
            else: raise CriterionFailure(name+' survived')
            case={'protection-return':1,'onfault-pc':5,'unregistered-return':4,
                  'real-mmio':6,'classification':1}[name]
            qout=dest/'qemu'; elf=image(qout,mutant,case)
            try: observe(qout,elf,case)
            except CriterionFailure as e:
                (qout/'RED.log').write_text(str(e)); print(name+': QEMU runtime RED',flush=True)
            else: raise CriterionFailure(name+' QEMU survived')
    print('kboot-trap PASS',flush=True)
if __name__=='__main__': main()
