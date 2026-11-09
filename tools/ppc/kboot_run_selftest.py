#!/usr/bin/env python3
"""Exercise the actual acceptance meter with OF-loaded synthetic kernels."""
import os
from pathlib import Path
import sys
import time
import types
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
static void emit(const char *s) { unsigned n=0; while(s[n]) ++n; pm_escc_write(&sink,s,n); pm_escc_drain(&sink); }
static unsigned tb(void) { unsigned t; __asm__ volatile("mftb %0":"=r"(t)); return t; }
void fixture_stop(void);
void ppc_kernel_start(ppc_ci ci) {
 struct ofw_env e; ofw_init(ci);
 if(ofw_setup(&e) || pm_escc_prepare(&e,&sink)) fixture_stop();
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
'''


def fixture(work, lines, alive, wait=True, stop=False):
    work.mkdir(parents=True, exist_ok=True)
    saved_c, saved_s = entry.CANARY, entry.OBSERVER
    entry.CANARY = SOURCE.replace('@LINES@', '\n'.join('emit("KBOOT '+s+'\\r\\n");' for s in lines)).replace(
        '@WAIT@', 'unsigned start=tb(); while((unsigned)(tb()-start)<220000000) {}' if wait else '').replace(
        '@ALIVE@', 'emit("'+alive+'\\r\\n");'+('fixture_stop();' if stop else ''))
    entry.OBSERVER = ASM
    try:
        entry.build(work, (ROOT/'arch/ppc/kernel_entry.S').read_text())
    finally:
        entry.CANARY, entry.OBSERVER = saved_c, saved_s
    return work/'kernel.elf'


def main():
    import argparse
    parser=argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true'); args=parser.parse_args()
    started=time.monotonic(); out=Path(os.environ['TMPDIR'])/'kboot-selftest'
    good='KBOOT ALIVE tick_delta=2200 tb_us=2200000 of_calls_delta=0'
    elf=fixture(out/'good', LINES, good)
    for cpu in ('7450','7455'):
        meter.accept(elf,cpu,60,out/('good-'+cpu)); print('GREEN '+cpu,flush=True)
    cases=[('order', [LINES[1],LINES[0],*LINES[2:]],good,False, 'milestone 1 order/format'),
           ('missing', LINES[:3]+LINES[4:],good,False,'milestone 4 order/format'),
           ('fatal', LINES+['FATAL fixture'],good,False,'forbidden diagnostic'),
           ('tick', LINES,good.replace('2200 tb','1 tb'),False,'ALIVE tick/TB mismatch'),
           ('stop',LINES,good,True,'stopped after READY'),
           ('duplicate',LINES+[LINES[-1]],good,False,'milestone 9 order/format'),
           ('duration',LINES,good.replace('tick_delta=2200','tick_delta=1000').replace('2200000','1000000'),False,'ALIVE duration')]
    images = {}
    for name,lines,alive,stop,reason in cases:
        elf=fixture(out/name,lines,alive,stop=stop)
        images[name] = elf
        try: meter.accept(elf,'7450',60,out/(name+'-guest'))
        except CriterionFailure as exc:
            assert str(exc)==reason, (name,str(exc),reason)
            # Evidence must include the reached marker, never just a timeout.
            raw=(out/(name+'-guest')/'serial-a.log').read_text()
            assert 'KBOOT '+lines[0] in raw
            (out/(name+'-guest')/'red.txt').write_text(str(exc))
            print('RED '+name+': '+str(exc),flush=True)
        else: raise AssertionError('SURVIVED '+name)
    elf=fixture(out/'instant',LINES,good,wait=False)
    try: meter.accept(elf,'7450',60,out/'instant-guest')
    except CriterionFailure as exc:
        assert str(exc)=='QMP READY duration', str(exc)
        print('RED instant: '+str(exc),flush=True)
    else: raise AssertionError('SURVIVED instant')
    elf=fixture(out/'negative',LINES[:2]+['FATAL fixture'],'',wait=False,stop=True)
    meter.accept(elf,'7450',30,out/'negative-guest','fixture-fatal')
    if args.mutate:
        source=(ROOT/'tools/ppc/kboot_run.py').read_text()
        for name,before,after,case in METER_MUTATIONS:
            assert source.count(before)==1, name+' unique product anchor'
            mutant=types.ModuleType('meter_mutant')
            exec(compile(source.replace(before,after),str(ROOT/'tools/ppc/kboot_run.py'),'exec'),mutant.__dict__)
            # Stop mutation still has the entry-stop guard; fixture_stop is
            # distinct, so only the mutated instruction check catches it.
            mutant.accept(images[case],'7450',60,out/('mutant-'+name))
            raw=(out/('mutant-'+name)/'serial-a.log').read_text()
            assert 'KBOOT READY stage=pre-user' in raw
            print('RED product '+name+': invalid guest accepted (runtime oracle)',flush=True)
    print(f'PASS 8 fixture RED, {len(METER_MUTATIONS) if args.mutate else 0} product RED elapsed={time.monotonic()-started:.2f}s')


if __name__=='__main__': main()
