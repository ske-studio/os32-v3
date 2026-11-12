#!/usr/bin/env python3
"""F2 ILP32 product DEC and mac99 D1/D2/F1/F2 runtime tests."""
import argparse
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
import kboot_vectors as v
from run import command, require
CHECK = dict(name='kboot-dec', tier=2, budget=600,
             covers=v.CHECK['covers']+['arch/ppc/kernel_dec.c','arch/ppc/arch_io.h',
                 'tools/ppc/kboot_dec*','platform/powermac/clock.c',
                 'kernel/time/tick.c','kernel/irq/tick.c'],
             inputs=['kernel/time/tick_clock.h','sdk/include/os32/os32_kapi_shared.h',
                 'include/kernel_tick.h','kernel/irq/tick.h','kernel/irq/irq.h',
                 'kernel/irq/irq_math.h','include/io.h',
                 'include/clock_source.h','include/platform_timer.h',
                 'platform/powermac/kboot_info.h','lib/divdi3.c'])
# Expected runtime diagnostic and the last reached check are both mandatory.
MUTATIONS = [
    ('reload', 'ppc_write_dec((u32)remaining);', '(void)remaining;', 8),
    ('fraction', 'fraction += remainder;', 'fraction += 0;', 13),
    ('catchup', '++elapsed;', 'elapsed = 1;', 24),
    ('initial-dec', 'ppc_write_dec(next_period());', '(void)next_period();', 7),
    ('1khz', 'tick_hz != PM_DEC_HZ', 'tick_hz != 100', 5),
    ('jiffy-gate', 'if (tick_clock_step() & TICK_STEP_JIFFY) tick_dispatch();',
     '{ (void)tick_clock_step(); tick_dispatch(); }', 3),
    ('step-call', 'if (tick_clock_step() & TICK_STEP_JIFFY) tick_dispatch();',
     'if (0) tick_dispatch();', 24),
    ('irq-depth', '++kctx_irq_depth;', '(void)kctx_irq_depth;', 20),
    ('ready', 'ppc_write_dec(PPC_DEC_FAR);', 'ppc_write_dec(1);', 4),
]

def host(out, source, expected=0, idle=False):
    out.mkdir(parents=True, exist_ok=True)
    exe=out/'host'
    command(['gcc','-m32','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
             '-ffreestanding','-fno-pie','-no-pie','-nostdlib','-static',
             *(['-DDEC_IDLE_TEST'] if idle else []),
             '-I'+str(out),'-I'+str(v.ROOT),'-I'+str(v.ROOT/'include'),
             '-I'+str(v.ROOT/'arch/ppc'),'-I'+str(v.ROOT/'platform/powermac'),
             '-I'+str(v.ROOT/'sdk/include/os32'),
             source,v.ROOT/'kernel/time/tick.c',v.ROOT/'kernel/irq/tick.c',
             v.ROOT/'tools/ppc/kboot_dec_host.c',
             '-Wl,--wrap=tick_clock_config','-o',exe])
    result=subprocess.run(['qemu-i386',exe],capture_output=True,timeout=5)
    (out/'host.log').write_bytes(result.stdout+result.stderr)
    mark=expected if expected else (18 if idle else 17)
    require(result.returncode==expected and result.stdout==mark.to_bytes(4,'little'),
            f'host failure (not RED): rc={result.returncode}, marks={result.stdout!r}, expected={expected}/{mark}')
    print(('RED' if expected else 'GREEN')+f': host rc={expected}, reach={mark}',flush=True)


def image(out, source):
    out.mkdir(parents=True,exist_ok=True)
    text=(v.ROOT/'tools/ppc/kboot_vectors_fixture.c').read_text()
    a=text.index('void product_trap_dispatch(')
    b=text.index('void fixture_setup_real(',a)
    text=text[:a]+(v.ROOT/'tools/ppc/kboot_dec_fixture.inc').read_text()+text[b:]
    # Retain the D1 real takeover/page tables/stacks and product spill path.
    text=text.replace('#include "ppc_trap.h"', '#include "ppc_trap.h"\n#include "kboot_info.h"\n#include "platform_timer.h"\n#include "clock_source.h"\n#include "kernel_tick.h"')
    text=text.replace('    ppc_disable();\n    fixture_real();',
                      '    struct pm_kboot_info info={0};\n    int cpu=ofw_child(ofw_finddevice("/cpus"));\n    if (ofw_getprop(cpu,"timebase-frequency",&info.tb_hz,4)!=4) ppc_stop();\n    pm_clock_bind(&info);\n    ppc_disable();\n    fixture_real();')
    # Remove D1-specific dispatch canary state, keep spill validation dormant.
    text=text.replace('static volatile u32 kind, dispatches, spills, nested, in_dispatch;',
                      'static volatile u32 spills, nested, in_dispatch;')
    fixture=out/'fixture.c'; fixture.write_text(text)
    previous=v.PRODUCT
    previous_flags=v.FLAGS
    v.FLAGS=previous_flags+['-I'+str(v.ROOT/'sdk/include/os32')]
    v.PRODUCT=[p for p in previous if p!='tools/ppc/kboot_vectors_fixture.c']+[
        str(fixture),str(source),'platform/powermac/clock.c',
        'kernel/time/tick.c','kernel/irq/tick.c','lib/divdi3.c']
    try: return v.build(out,v.ROOT/'arch/ppc/kernel_vectors.S')
    finally: v.PRODUCT=previous; v.FLAGS=previous_flags


def observe(out, elf, red=None):
    # Use the existing bounded QMP observer, with this stage's terminal criteria.
    import inspect
    body=inspect.getsource(v.observe)
    body=body.replace("'ppc_kernel_vector_stop']", "'ppc_kernel_vector_stop','ppc_trap_stop']")
    a=body.index('            if red is not None:')
    b=body.index("            q.call('quit')",a)
    body=body[:a]+'''            require(b'F2-BEGIN\\r\\n' in serial,'missing foreground reach mark')
            metrics={}
            for name in ('f2_delta','f2_ticks','f2_hz'):
                value=q.call('human-monitor-command',{'command-line':'xp /1wx '+hex(syms[name])})
                metrics[name]=int(value.split()[-1],16)
            (out/'metrics.log').write_text(repr(metrics))
            if red is None:
                ratio=metrics['f2_ticks']*metrics['f2_hz']/(metrics['f2_delta']*100)
                require(0.95<=ratio<=1.05,'independent TB/tick ratio '+str(ratio))
                require(last==syms['fixture_done'],'unexpected baseline stop '+repr(serial))
                require(serial[serial.index(b'F2-BEGIN'):]==b'F2-BEGIN\\r\\nF2-ARMED\\r\\nF2-PASS\\r\\n',
                        'unexpected serial output / IRQ log '+repr(serial))
            else:
                stop, markers=red
                require(last==syms[stop], 'unexpected mutant stop '+hex(last or 0))
                for marker in markers:
                    require(serial.count(marker)==1,'missing mutant diagnostic '+repr(marker))
                (out/'RED.log').write_text('stop='+stop+' markers='+repr(markers))
''' +body[b:]
    body=body.replace('QEMU canary/retry/dispatch/nested spill PASS','QEMU F2 2s TB/tick verdict')
    scope=vars(v).copy()
    exec(body,scope)
    scope['observe'](out,elf,red)


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--mutate',action='store_true')
    args=parser.parse_args()
    out=v.ROOT/'build/out/ppc/kboot-dec'
    source=v.ROOT/'arch/ppc/kernel_dec.c'
    host(out/'host',source)
    host(out/'idle',source,idle=True)
    elf=image(out/'normal',source)
    (out/'normal/readelf.log').write_text(command([str(v.CROSS)+'readelf','-hSW',elf]))
    (out/'normal/objdump.log').write_text(command([str(v.CROSS)+'objdump','-d',elf]))
    require(not command([str(v.CROSS)+'nm','-u',elf]).strip(),'unresolved helpers')
    observe(out/'normal',elf)
    if args.mutate:
        original=source.read_text()
        for name,old,new,expected in MUTATIONS:
            require(original.count(old)==1,'unique anchor '+name)
            dest=out/name; dest.mkdir(parents=True,exist_ok=True)
            mutant=dest/'kernel_dec.c'; mutant.write_text(original.replace(old,new))
            host(dest,mutant,expected)
            # Lost rearm and lost first DEC must reach a bounded TB-based verdict,
            # rather than relying on a QEMU timeout as RED.
            if name in ('reload','initial-dec','1khz'):
                elf=image(dest/'qemu',mutant)
                diagnostic=b'F2-START-FAIL' if name=='1khz' else b'F2-RATE-FAIL'
                observe(dest/'qemu',elf,('ppc_stop_loop',[b'F2-BEGIN',diagnostic]))
        dest=out/'idle-noop'; dest.mkdir(parents=True,exist_ok=True)
        header=(v.ROOT/'arch/ppc/arch_io.h').read_text()
        old='static inline void _idle(void) { arch_cpu_idle(); }'
        require(header.count(old)==1,'unique migrated idle anchor')
        (dest/'arch_io.h').write_text(header.replace(old,'static inline void _idle(void) { }'))
        host(dest,source,18,idle=True)
    print('kboot-dec PASS',flush=True)

if __name__=='__main__': main()
