#!/usr/bin/env python3
"""Expectation origin: PPC_FP_NEWLIB_DESIGN §8 F1〜F5・B1.

PM system-QEMU acceptance scaffold. Emit unique product mutants and the
observation contract; this command does not claim guest acceptance. The USER
fixture uses fp_probe.S load/store across syscall, park/resume and nested exec;
IRQ and kernel-callback injection must be attached by the PM acceptance stage.
"""
import argparse
import json
import os
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('F1-entry', 'arch/ppc/exec_enter.S', 'PPC_MSR_ME|PPC_MSR_FP|PPC_MSR_IR', 'PPC_MSR_ME|PPC_MSR_IR'),
    ('F2-fpr17', 'arch/ppc/fp_context.S', '    stfd 17,136(3)', '    nop'),
    ('F2-fpscr', 'arch/ppc/fp_context.S', '    stfd 0,PPC_FP_FPSCR_OFFSET(3)', '    nop'),
    ('F3-restore', 'arch/ppc/kernel_fp.c', '    arch_fp_restore(&g->previous);', '    (void)g;'),
    ('F4-retry', 'arch/ppc/kernel_trap.c', '    if (!f || !translated) goto fatal;',
     '    if (!f || !translated) goto fatal;\n    if (f->vector == 0x800) return;'),
    ('F5-init', 'arch/ppc/fp_context.S', '    b arch_fp_restore', '    blr'),
)
OBSERVATIONS = {
    'F1': 'first USER fadd succeeds; entry mutation faults with vector 0x800',
    'F2': 'all 32 FPR bit patterns and FPSCR RN/status survive A/B park/resume, nested exec, restart',
    'F3': 'kernel guard callback clobbers every FPR/FPSCR; USER syscall and IRQ return preserve bits',
    'F4': 'FP-disabled USER is killed with FP_UNAVAILABLE; IRQ FP misuse is fatal; retry mutant times out',
    'F5': 'slot reuse after fault/kill/exit starts with zero FPR/FPSCR, FD/heap owners reclaimed',
    'B1': 'four image audits/size ASSERT; ordinary kernel FP fixture refused by runtime audit',
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cpu', choices=('7450', '7455'), default='7450')
    ap.add_argument('--plan', action='store_true')
    ap.add_argument('--emit-mutants', type=Path)
    args = ap.parse_args()
    cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-gcc'
    if not shutil.which(str(cc)):
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP')+': PPC toolchain unavailable')
        return int(required)
    plan = dict(status='PLAN ONLY: privileged FP acceptance not executed', cpu=args.cpu,
                positive=['make', '-f', 'build/ppc-kboot.mk', 'KBOOT_SH=1', 'KBOOT_LINK=1'],
                observations=OBSERVATIONS, mutations=[])
    for name, source, old, new in MUTATIONS:
        text = (ROOT/source).read_text()
        assert text.count(old) == 1, name+' requires a unique product anchor'
        item = dict(name=name, source=source)
        if args.emit_mutants:
            args.emit_mutants.mkdir(parents=True, exist_ok=True)
            path = (args.emit_mutants/(name+Path(source).suffix)).resolve()
            path.write_text(text.replace(old, new))
            item['settings'] = ['KB_MUT_SRC='+source, 'KB_MUT_FILE='+str(path)]
        plan['mutations'].append(item)
    print(json.dumps(plan, indent=2, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
