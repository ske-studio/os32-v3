#!/usr/bin/env python3
"""Milestone-2 acceptance of the real PowerPC kernel (PPC_KBOOT_DESIGN section 2).

Builds build/ppc-kboot.mk, then requires on QEMU mac99:
  - the nine KBOOT milestones, >=2 s READY and a continuing 1 kHz tick (7450 and 7455);
  - each registered negative image: reached marker -> exact FATAL -> stop symbol;
  - with --mutate: each product mutation yields exactly its expected meter verdict.
A build failure, QEMU startup failure, timeout or any other verdict is a test
error (non-zero exit), never a RED. Evidence is kept under $TMPDIR/kboot-accept/.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
sys.dont_write_bytecode = True
from run import ROOT, CriterionFailure
import kboot_run as meter
from kboot_mutations import KERNEL_NEGATIVES, KERNEL_MUTATIONS, AUDIT_MUTATIONS
import re

CHECK = dict(name='ppc-kboot-accept', tier=2, budget=900, covers=[
    'init/ppc_kernel.c', 'arch/ppc/**', 'platform/powermac/**', 'mm/**', 'kernel/sys.c',
    'kernel/irq/tick.c', 'kernel/time/tick.c', 'fs/fd_redirect.c', 'lib/kprintf.c',
    'lib/kstring_c.c', 'lib/divdi3.c', 'include/**', 'build/ppc-kboot.mk',
    'tools/ppc/kboot_accept.py', 'tools/ppc/kboot_run.py', 'tools/ppc/kboot_mutations.py',
    'tools/ppc/run.py', 'tools/gen_memmap.py'])


def make(out, *settings):
    result = subprocess.run(['make', '--no-print-directory', '-f', 'build/ppc-kboot.mk',
                             'KB_OUT='+str(out), *settings], cwd=ROOT, text=True,
                            capture_output=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    (out.parent/(out.name+'.build.log')).write_text(result.stdout+result.stderr)
    return result


def build(out, *settings):
    """Returns the ELF; make owns the manifest and the undefined/FP/PIT_HZ audits."""
    result = make(out, *settings)
    if result.returncode or 'kboot audit OK' not in result.stdout:
        raise RuntimeError('kboot build failed: '+str(out)+'\n'+(result.stdout+result.stderr)[-2000:])
    return out/'PPCKERN.ELF'


def mutant_settings(work, name, source, before, after):
    text = (ROOT/source).read_text()
    if text.count(before) != 1:
        raise RuntimeError(name+': mutation anchor must be unique in '+source)
    mutant_source = work/('mutant-'+name+Path(source).suffix)
    mutant_source.write_text(text.replace(before, after))
    return ['KB_MUT_SRC='+source, 'KB_MUT_FILE='+str(mutant_source)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    args = parser.parse_args()
    started = time.monotonic()
    base = Path(os.environ.get('TMPDIR') or ROOT/'build/out/ppc/tmp')/'kboot-accept'
    base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    elf = build(work/'kernel')
    skipped = False
    for cpu in cpus:
        evidence = meter.accept(elf, cpu, 60, work/('normal-'+cpu))
        skipped |= (evidence/'lan-skip').exists()
        lines = [l for l in (evidence/'serial-a.log').read_text().replace('\r', '').splitlines()
                 if l.startswith('KBOOT ')]
        print('GREEN '+cpu+': '+lines[-1], flush=True)
        meter.accept(elf, cpu, 60, work/('lan-off-'+cpu), lan_off=True)
        print('GREEN LAN OFF '+cpu, flush=True)
    for name, number in KERNEL_NEGATIVES.items():
        negative = build(work/name, 'NEGATIVE='+str(number))
        for cpu in cpus:
            meter.accept(negative, cpu, 30, work/(name+'-'+cpu), name)
            print('GREEN negative '+name+' '+cpu, flush=True)
    red = 0
    if args.mutate:
        for name, source, before, after, expected, evidence_line in KERNEL_MUTATIONS:
            mutant = build(work/('mutant-'+name),
                           *mutant_settings(work, name, source, before, after))
            guest = work/('mutant-'+name+'-guest')
            try:
                meter.accept(mutant, '7450', 60, guest)
            except CriterionFailure as exc:
                # The verdict alone does not identify the cause: require the one
                # serial line that does, and nothing else failing.
                serial = next(guest.iterdir())/'serial-a.log'
                lines = serial.read_text().replace('\r', '').splitlines()
                causes = [l for l in lines if re.search(evidence_line, l)]
                if str(exc) != expected or len(causes) != 1:
                    raise RuntimeError(f'{name}: verdict "{exc}" evidence={causes}, '
                                       f'expected "{expected}" and /{evidence_line}/')
                print(f'RED {name}: {exc} | {causes[0]}', flush=True)
                red += 1
            else:
                raise RuntimeError('SURVIVED '+name)
        for name, source, before, after, finding in AUDIT_MUTATIONS:
            result = make(work/('audit-'+name),
                          *mutant_settings(work, name, source, before, after))
            output = result.stdout+result.stderr
            # The image must link; only the audit may reject it, for this reason.
            if not (result.returncode and 'kboot audit FAILED' in output and finding in output
                    and (work/('audit-'+name)/'PPCKERN.ELF').exists()):
                raise RuntimeError(f'{name}: audit did not report "{finding}"\n'+output[-1500:])
            print(f'RED audit {name}: {finding}', flush=True)
            red += 1
    print(f'PASS kernel acceptance: {len(cpus)} CPU GREEN, {len(KERNEL_NEGATIVES)} negatives, '
          f'{red} product/audit RED, evidence={work} elapsed={time.monotonic()-started:.1f}s')
    if skipped:
        print('SKIP LAN socket rc=3')
        return 3
    return 0


if __name__ == '__main__':
    sys.exit(main())
