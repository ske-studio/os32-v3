#!/usr/bin/env python3
"""Exercise the actual acceptance meter with OF-loaded synthetic kernels."""
import os
from pathlib import Path
import sys
import time
import types
import tempfile
from kboot_mutations import METER_MUTATIONS
sys.dont_write_bytecode = True
import kboot_entry as entry
import kboot_run as meter
from run import ROOT, command, CriterionFailure

CHECK = dict(name='ppc-kboot-run-selftest', tier=2, budget=600,
             covers=['tools/ppc/kboot_run*.py', 'tools/ppc/kboot_mutations.py',
                     'tools/ppc/kboot_entry.py', 'tools/ppc/run.py',
                     'arch/ppc/kernel_entry.*', 'arch/ppc/kernel.ld',
                     'platform/powermac/**', 'lib/kstring*', 'include/types.h',
                     'include/memmap.h', 'tools/gen_memmap.py', 'build/arch/ppc.mk'])
LINES = [
 'OF-CAPTURE ram=134217728 admitted=67108864 tb_hz=100000000',
 'OF-OFF calls=1', 'MMU-D root=2097152 hpt=1048576 bytes=1048576 ram_bat=0',
 'MEMORY online=1 heap=1 shm=1', 'TRAP dsi=pass isi=pass guard=pass flush=pass',
 'MPIC sources=64 priority=0 cpu=0 delivery=pass', 'CLOCK hz=1000 tick=1 us=1000',
 'READY stage=pre-user']
SOURCE = r'''
#include "kernel_entry.h"
#include "ofw.h"
static struct pm_escc sink;
volatile unsigned long long kboot_ready_tb;
volatile unsigned kboot_tick;
volatile unsigned kboot_reload = 100000;
extern unsigned irq_start[], irq_end[];
static void clock_init(void) {
 unsigned old, real;
 __asm__ volatile("mfmsr %0":"=r"(old)); real=old & ~0x10u;
 __asm__ volatile("mtmsr %0; isync"::"r"(real):"memory");
 unsigned *p=(unsigned *)0x900, *s=irq_start;
 while(s<irq_end) *p++=*s++;
 __asm__ volatile("sync; icbi 0,%0; sync; isync"::"r"(0x900):"memory");
 __asm__ volatile("mtmsr %0; isync"::"r"(old):"memory");
 unsigned dec=100000, msr;
 __asm__ volatile("mtdec %0; mfmsr %1":"=r"(dec),"=r"(msr):"0"(dec));
 msr=(msr & ~0x40u)|0x8000u;
 __asm__ volatile("mtmsr %0; isync"::"r"(msr):"memory");
}
static void emit(const char *s) { unsigned n=0; while(s[n]) ++n; pm_escc_write(&sink,s,n); pm_escc_drain(&sink); }
static unsigned tb(void) { unsigned t; __asm__ volatile("mftb %0":"=r"(t)); return t; }
void fixture_stop(void);
void ppc_kernel_start(ppc_ci ci) {
 struct ofw_env e; ofw_init(ci);
 if(ofw_setup(&e) || pm_escc_prepare(&e,&sink)) fixture_stop();
 clock_init();
 @LINES@
 @WAIT@
 @ALIVE@
 for (;;) { __asm__ volatile("nop; nop; nop" ::: "memory"); }
}
'''
ASM = entry.OBSERVER[entry.OBSERVER.index('.globl ppc_load8'):]+'''
.globl fixture_stop
fixture_stop:
 b fixture_stop
.globl irq_start, irq_end
irq_start:
 /* Periodic-add reload: a late handler (TCG host scheduling) must add whole
  * periods until DEC is positive again, or the edge-triggered exception is lost. */
 mtspr 272,0
 mtspr 273,3
 mtspr 274,4
 mfcr 3
 mtspr 275,3
 lis 3,kboot_reload@ha
 lwz 3,kboot_reload@l(3)
 mfdec 0
 li 4,0
1: add 0,0,3
 addi 4,4,1
 cmpwi 0,0
 ble 1b
 mtdec 0
 lis 3,kboot_tick@ha
 lwz 0,kboot_tick@l(3)
 add 0,0,4
 stw 0,kboot_tick@l(3)
 mfspr 3,275
 mtcr 3
 mfspr 4,274
 mfspr 3,273
 mfspr 0,272
 rfi
irq_end:
'''


def fixture(work, lines, alive, wait=True, stop=False, tail="", immediate=False):
    work.mkdir(parents=True, exist_ok=True)
    saved_c, saved_s = entry.CANARY, entry.OBSERVER
    entry.CANARY = SOURCE.replace('@LINES@', '\n'.join(('kboot_ready_tb=tb();' if s==LINES[-1] else '')+'emit("'+('' if s.startswith('FATAL') else 'KBOOT ')+s+'\\r\\n");' for s in lines)).replace(
        '@WAIT@', 'unsigned start=tb(); while((unsigned)(tb()-start)<'+('200000000' if immediate else '220000000')+') {}' if wait else '').replace(
        '@ALIVE@', 'emit("'+alive+'\\r\\n");'+('unsigned m; __asm__ volatile("mfmsr %0":"=r"(m)); m &= ~0x8000u; __asm__ volatile("mtmsr %0; isync"::"r"(m)); fixture_stop();' if stop else '')+tail)
    entry.OBSERVER = ASM
    try:
        entry.build(work, (ROOT/'arch/ppc/kernel_entry.S').read_text())
    finally:
        entry.CANARY, entry.OBSERVER = saved_c, saved_s
    return work/'kernel.elf'


def main():
    import argparse
    parser=argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true'); args=parser.parse_args()
    started=time.monotonic(); out=Path(tempfile.mkdtemp(prefix='kboot-selftest-', dir=os.environ['TMPDIR']))
    good='KBOOT ALIVE tick_delta=2200 tb_us=2200000 of_calls_delta=0'
    elf=fixture(out/'good', LINES, good)
    normal = {}
    for cpu in ('7450','7455'):
        normal[cpu]=meter.accept(elf,cpu,60,out/('good-'+cpu))
        print('GREEN '+cpu,flush=True)
    retained = {p.name:p.read_bytes() for p in normal['7450'].iterdir() if p.is_file()}
    cases=[('order', [LINES[1],LINES[0],*LINES[2:]],good,False, 'milestone 1 order/format'),
           ('missing', LINES[:3]+LINES[4:],good,False,'milestone 4 order/format'),
           ('fatal', LINES+['FATAL fixture'],good,False,'forbidden diagnostic'),
           ('tick', LINES,good.replace('2200 tb','1 tb'),False,'ALIVE tick/TB mismatch'),
           ('stop',LINES,good,True,'stopped after READY'),
           ('duplicate',LINES+[LINES[-1]],good,False,'duplicate/extra milestone'),
           ('duration',LINES,good.replace('tick_delta=2200','tick_delta=1000').replace('2200000','1000000'),False,'ALIVE duration')]
    images = {}
    for name,lines,alive,stop,reason in cases:
        elf=fixture(out/name,lines,alive,stop=stop)
        images[name] = elf
        try: meter.accept(elf,'7450',60,out/(name+'-guest'))
        except CriterionFailure as exc:
            assert str(exc)==reason, (name,str(exc),reason)
            # Evidence must include the reached marker, never just a timeout.
            evidence=next((out/(name+'-guest')).iterdir())
            raw=(evidence/'serial-a.log').read_text()
            assert 'KBOOT '+lines[0] in raw
            (evidence/'red.txt').write_text(str(exc))
            print('RED '+name+': '+str(exc),flush=True)
        else: raise AssertionError('SURVIVED '+name)
    elf=fixture(out/'instant',LINES,good,wait=False)
    try: meter.accept(elf,'7450',60,out/'instant-guest')
    except CriterionFailure as exc:
        assert str(exc)=='QMP READY duration', str(exc)
        print('RED instant: '+str(exc),flush=True)
    else: raise AssertionError('SURVIVED instant')
    elf=fixture(out/'negative',LINES[:2]+['FATAL fixture'],'',wait=False,stop=True)
    negative=meter.accept(elf,'7450',30,out/'good-7450','fixture-fatal')
    assert negative != normal['7450']
    assert retained == {p.name:p.read_bytes() for p in normal['7450'].iterdir() if p.is_file()}
    print('GREEN negative contract and retained evidence',flush=True)
    # Correct ALIVE immediately after exactly two guest seconds must pass.
    elf=fixture(out/'exact',LINES,good.replace('2200','2000'),immediate=True)
    meter.accept(elf,'7450',60,out/'exact-guest')
    print('GREEN exact READY duration',flush=True)
    # EE-enabled self branch resumes via the decrementer handler.
    elf=fixture(out/'busy',LINES,good,tail='__asm__ volatile("1: b 1b");')
    meter.accept(elf,'7450',60,out/'busy-guest')
    print('GREEN EE busy idle',flush=True)
    extra=[('late-stop', LINES, good,
            'unsigned start2=tb(); while((unsigned)(tb()-start2)<5000000) {} '
            'unsigned m; __asm__ volatile("mfmsr %0":"=r"(m)); m &= ~0x8000u; '
            '__asm__ volatile("mtmsr %0; isync"::"r"(m)); fixture_stop();',
            'stopped after READY'),
           ('raw-fatal',LINES,good,'emit("FATAL");','forbidden diagnostic'),
           # ALIVE is honest, then the tick drops to 10 Hz: must not pass on "tick moved".
           ('slow-tick',LINES,good,'kboot_reload=10000000;','QMP tick rate'),
           # EE cleared in a non-branch loop: not a stop PC, but no interrupts either.
           ('ee-off',LINES,good,'unsigned m2; __asm__ volatile("mfmsr %0":"=r"(m2)); m2 &= ~0x8000u; '
            '__asm__ volatile("mtmsr %0; isync"::"r"(m2));','interrupts disabled after READY'),
           ('early-fatal',['FATAL fixture']+LINES[:2], '', '', 'negative diagnostic/progress'),
           ('wrong-fatal',LINES[:2]+['FATAL fixture_wrong'], '', '', 'negative diagnostic/progress')]
    for name,lines,alive,tail,reason in extra:
        neg=name in ('early-fatal','wrong-fatal')
        elf=fixture(out/name,lines,alive,wait=not neg,stop=neg,tail=tail)
        images[name] = elf
        try: meter.accept(elf,'7450',30,out/(name+'-guest'),'fixture-fatal' if neg else None)
        except CriterionFailure as exc:
            assert str(exc)==reason,(name,str(exc))
            print('RED '+name+': '+str(exc),flush=True)
        else: raise AssertionError('SURVIVED '+name)
    if args.mutate:
        source=(ROOT/'tools/ppc/kboot_run.py').read_text()
        for name,before,after,case,expected in METER_MUTATIONS:
            assert source.count(before)==1, name+' unique product anchor'
            mutant=types.ModuleType('meter_mutant')
            exec(compile(source.replace(before,after),str(ROOT/'tools/ppc/kboot_run.py'),'exec'),mutant.__dict__)
            try:
                evidence=mutant.accept(images[case],'7450',60,out/('mutant-'+name))
            except mutant.CriterionFailure as exc:
                # Only the next guard's exact reason proves the mutated line owned the verdict.
                assert expected!='ACCEPT' and str(exc)==expected, (name,str(exc),expected)
                print('RED product '+name+': verdict moved to "'+str(exc)+'" (runtime oracle)',flush=True)
            else:
                assert expected=='ACCEPT', (name,'accepted, expected '+expected)
                raw=(evidence/'serial-a.log').read_text()
                assert 'KBOOT READY stage=pre-user' in raw
                print('RED product '+name+': invalid guest accepted (runtime oracle)',flush=True)
    print(f'PASS 14 fixture RED, 4 GREEN, {len(METER_MUTATIONS) if args.mutate else 0} product RED elapsed={time.monotonic()-started:.2f}s')


if __name__=='__main__': main()
