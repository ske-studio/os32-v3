#!/usr/bin/env python3
"""User decision 2026-11-16 and mainline assignment (PPC NAP)."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

CHECK = dict(name='ppc-cpu-nap', tier=2, budget=30, needs=[], env=['ppc-cross'],
    covers=['tools/tests/test_ppc_cpu_nap.py', 'tools/ppc/cpu_nap*', 'arch/ppc/kernel_cpu.c',
            'arch/ppc/kernel_dec.c', 'arch/ppc/kernel_trap.c',
            'platform/powermac/kernel_boot.c', 'arch/ppc/mmu.S', 'arch/ppc/ppc_oea.h',
            'arch/ppc/ppc_trap.h', 'arch/ppc/ppc_frame.h', 'arch/ppc/pd_walk.c',
            'arch/ppc/ppc_pd.h', 'arch/ppc/ppc_boot.h', 'arch/ppc/arch_exec_storage.h',
            'include/exec_arch.h', 'include/types.h', 'include/callconv.h',
            'kernel/irq/irq.h', 'kernel/irq/irq_math.h', 'include/platform_irq.h',
            'platform/powermac/platform_irq_limits.h', 'arch/ppc/ppc_hpt.h',
            'platform/powermac/platform_of_layout.h'],
    basis=['User decision 2026-11-16 and mainline assignment (PPC NAP)'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('ignore-ee', 'idle', '!ppc_irq_enabled() || kctx_irq_depth != 0', 'kctx_irq_depth != 0', 11),
    ('nap-in-irq', 'idle', '!ppc_irq_enabled() || kctx_irq_depth != 0', '!ppc_irq_enabled()', 12),
    ('no-nap', 'idle', '    ppc_cpu_nap();', '    (void)0;', 10),
    ('keep-pow', 'trap', 'f->srr1 &= ~PPC_MSR_POW;', 'f->srr1 &= ~0u;', 40),
    ('clear-bit8', 'power', '(hid & ~PPC_HID_SLEEP)', '(hid & ~(PPC_HID_SLEEP | PPC_HID_HIGHBAT))', 20),
    ('no-dpm', 'power', '| PPC_HID_NAP | PPC_HID_DPM', '| PPC_HID_NAP', 20),
]

def main(argv=None):
    parser=argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args=parser.parse_args(argv)
    cross=Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-gcc'
    missing=[x for x in (str(cross), 'gcc', 'qemu-i386') if not shutil.which(x)]
    if missing:
        print('SKIP ppc-cpu-nap: unavailable '+', '.join(missing), flush=True)
        return int(os.environ.get('OS32_REQUIRE_PPC')=='1')
    cpu=(ROOT/'arch/ppc/kernel_cpu.c').read_text()
    dec=(ROOT/'arch/ppc/kernel_dec.c').read_text()
    parts={'power': cpu[cpu.index('void ppc_cpu_power_init('):cpu.index('void ppc_cpu_load_segments(')],
           'idle': dec[dec.index('void arch_cpu_idle('):dec.index('void arch_cpu_halt(')],
           'trap': (ROOT/'arch/ppc/kernel_trap.c').read_text()}
    with tempfile.TemporaryDirectory(prefix='cpu-nap-') as td:
        out=Path(td)
        for name, part, old, new, expected in [('normal', None, None, None, 0)]+(MUTATIONS if args.mutate else []):
            current=parts.copy()
            if part:
                assert current[part].count(old)==1, name+' anchor'
                current[part]=current[part].replace(old,new)
            for key, text in current.items():
                (out/(key+('.c' if key=='trap' else '.inc'))).write_text(text)
            cmd=['gcc','-m32','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
                 '-ffreestanding','-nostdlib','-nostdinc','-static','-fno-pie','-no-pie',
                 '-fno-stack-protector','-DPPC_TRAP_HOST_TEST',
                 *['-I'+str(p) for p in (out,ROOT/'arch/ppc',ROOT/'include',ROOT/'platform/powermac',ROOT)],
                 str(ROOT/'tools/ppc/cpu_nap_host.c'),str(out/'trap.c'),
                 str(ROOT/'arch/ppc/pd_walk.c'),'-o',str(out/'host')]
            subprocess.run(cmd,check=True)
            rc=subprocess.run(['qemu-i386',str(out/'host')],timeout=5).returncode
            assert rc==expected, (name,rc,expected)
            print(('RED runtime: ' if part else 'GREEN: ')+name+' rc='+str(rc),flush=True)
    return 0

def run(ctx):
    return main(['--mutate'] if ctx.mutate else [])


if __name__=='__main__':
    raise SystemExit(main())
