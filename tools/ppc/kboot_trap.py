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

def host(out, source, expected=None):
    out.mkdir(parents=True,exist_ok=True)
    command(['gcc','-m32','-ffreestanding','-fno-pie','-no-pie','-nostdlib','-static','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
             '-DPPC_TRAP_HOST_TEST','-I'+str(v.ROOT/'arch/ppc'),
             '-I'+str(v.ROOT/'include'),'-I'+str(v.ROOT/'platform/powermac'),
             '-I'+str(v.ROOT),source,v.ROOT/'arch/ppc/pd_walk.c',
             v.ROOT/'tools/ppc/kboot_trap_host.c','-o',out/'host'])
    p=subprocess.run(['qemu-i386',out/'host'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/'host.log').write_bytes(p.stdout)
    if expected is None:
        require(p.returncode==0,'host runtime verdict '+str(p.returncode))
    else:
        require(p.returncode==expected and p.stdout[-4:]==expected.to_bytes(4,'little'),
                'unexpected host failure: rc='+str(p.returncode)+' marks='+repr(p.stdout))
        (out/'RED.log').write_text('expected/actual exit and last reach marker: '+str(expected))

def image(out, source, case):
    out.mkdir(parents=True,exist_ok=True)
    text=(v.ROOT/'tools/ppc/kboot_vectors_fixture.c').read_text()
    text=text.replace('#include "ppc_pd.h"','#include "ppc_pd.h"\n#include "ppc_trap.h"')
    a=text.index('void product_trap_dispatch('); b=text.index('void fixture_setup_real(',a)
    text=text[:a]+'''
volatile u32 fixture_reached;
static u32 table_read(u32 pa, void *ctx)
{ (void)ctx; return *(volatile u32 *)pa; }
static void hex(u32 n)
{ char s[9]; for(unsigned i=0;i<8;++i) s[i]="0123456789abcdef"[(n>>(28-4*i))&15]; s[8]=0; say(s); }
static void bad_real(void) __attribute__((noreturn,noinline));
static void bad_real(void)
{
    __asm__ volatile(".globl fixture_bad_real\\nfixture_bad_real: b fixture_bad_real");
    __builtin_unreachable();
}
void ppc_trap_fatal_sink(const struct ppc_frame *f,u32 reason)
{
    if ((msr()&0x8030)!=0x30 || serial.csr<IO_VA) {
        bad_real();
    }
    say("DUMP "); hex(reason); say(" PC "); hex(f->srr0);
    say(" VA "); hex(f->dar); say(" DSISR "); hex(f->dsisr); say("\\r\\n");
}
void fixture_main(void)
{
    say("D2-REACHED\\r\\n");
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
    fixture_reached=6;
    struct ppc_frame fatal={.vector=0x700};
    ppc_trap_dispatch(&fatal);
#endif
''')
    fixture=out/'fixture.c'; fixture.write_text(text)
    old_product,old_flags=v.PRODUCT,v.FLAGS
    v.PRODUCT=[p for p in old_product if p not in ('tools/ppc/kboot_vectors_fixture.c','arch/ppc/kernel_trap.c')]+[str(fixture),str(source)]
    v.FLAGS=old_flags+['-DTRAP_CASE='+str(case)]
    try: return v.build(out,v.ROOT/'arch/ppc/kernel_vectors.S')
    finally: v.PRODUCT,v.FLAGS=old_product,old_flags

def observe(out,elf,case, red=None):
    # Reuse D1's bounded terminal observer, substituting D2 stop/serial criteria.
    import inspect
    body=inspect.getsource(v.observe)
    body=body.replace("'ppc_kernel_vector_stop']","'ppc_kernel_vector_stop','ppc_trap_stop','fixture_bad_real']")
    start=body.index("            if red is not None:")
    end=body.index("            q.call('quit')",start)
    body=body[:start]+'''            expected = syms['fixture_done'] if case==2 else syms['ppc_trap_stop']
            require(last==expected,'D2 terminal PC '+hex(last or 0)+' '+repr(serial[-2000:]))
            if case==2:
                require(b'ONFAULT-PASS' in serial and b'DUMP' not in serial,'onfault continuation')
            elif case==6:
                require(b'DUMP' not in serial,'no real-mode sink')
            else:
                reason={1:3,3:0x110,4:9,5:3}[case]
                pc=syms['fixture_load' if case in (1,5) else 'fixture_fault_pc']
                marker=('DUMP %08x PC %08x'%(reason,pc)).encode()
                require(marker in serial,'fatal dump '+repr(serial[-2000:]))
                if case==1:
                    require(b'VA 50000000 DSISR 08000000' in serial,'protection syndrome')
''' +body[end:]
    if red is not None:
        start=body.index("            expected =")
        end=body.index("            q.call('quit')",start)
        body=body[:start]+'''            stop, markers = red
            if case==6:
                mark=q.call('human-monitor-command',{'command-line':'xp /1wx '+hex(syms['fixture_reached'])})
                require(mark.strip().endswith('0x00000006'),'real-mode reach mark '+mark)
            require(last==syms[stop], 'unexpected mutant stop '+hex(last or 0))
            for marker in markers:
                require(marker in serial,'missing mutant reach/diagnostic '+repr(marker))
            (out/'RED.log').write_text('expected/actual stop: '+stop+' marks: '+repr(markers))
''' +body[end:]
    body=body.replace('QEMU canary/retry/dispatch/nested spill PASS','QEMU D2 case '+str(case)+' PASS')
    scope=vars(v).copy(); scope['case']=case; scope['red']=red
    exec(body,scope)
    scope['observe'](out,elf,red)

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
                 ('classification','cause == PPC_DSISR_PROTECT','cause == 0x00400000'),
                 ('reader-unready','if (!table_reader || !ppc_root_current()) return PPC_TRAP_UNKNOWN;',
                  'if (!table_reader || !ppc_root_current()) return PPC_TRAP_DSI_MISS;'),
                 ('root-unready','if (!table_reader || !ppc_root_current()) return PPC_TRAP_UNKNOWN;',
                  'if (!table_reader) return PPC_TRAP_UNKNOWN;'),
                 ('isi-cause','return PPC_TRAP_ISI_PROTECT;', 'return PPC_TRAP_ISI;'),
                 ('guarded-miss','if (cause == PPC_DSISR_MISS && !mapped)',
                  'if (cause == PPC_DSISR_MISS && mapped && (t.attr & 8)) return PPC_TRAP_DSI_IO;\n    if (cause == PPC_DSISR_MISS && !mapped)'),
                 ('program-cause','cause == 0x00080000','cause == 0x00020000')]
        host_red={'protection-return':21,'onfault-pc':50,'unregistered-return':52,
                  'real-mmio':80,'classification':21,'reader-unready':40,
                  'root-unready':41,'isi-cause':61,'guarded-miss':42,'program-cause':66}
        qemu_red={
            'protection-return':(1,'ppc_stop_loop',[b'D2-REACHED',b'FAIL']),
            'onfault-pc':(5,'fixture_done',[b'D2-REACHED',b'ONFAULT-PASS']),
            'unregistered-return':(4,'ppc_stop_loop',[b'D2-REACHED',b'FAIL']),
            'real-mmio':(6,'fixture_bad_real',[]),
            'classification':(1,'ppc_trap_stop',[b'D2-REACHED',b'DUMP 00000001',
                                                     b'VA 50000000 DSISR 08000000'])}

        original=source.read_text()
        for name,old,new in changes:
            require(original.count(old)==1,'unique anchor '+name)
            dest=out/name; dest.mkdir(parents=True,exist_ok=True)
            mutant=dest/'kernel_trap.c'; mutant.write_text(original.replace(old,new))
            host(dest,mutant,host_red[name])
            print(name+': host runtime RED',flush=True)
            if name not in qemu_red: continue
            case, stop, markers=qemu_red[name]
            qout=dest/'qemu'; elf=image(qout,mutant,case)
            observe(qout,elf,case,(stop,markers))
            print(name+': QEMU runtime RED',flush=True)
    print('kboot-trap PASS',flush=True)
if __name__=='__main__': main()
