#!/usr/bin/env python3
"""Check the PPC jump context and its exception-depth mutation."""
import argparse
from pathlib import Path
import re

CHECK = dict(name='ppc-setjmp', tier=1, budget=10, needs=[],
             covers=['arch/ppc/setjmp.S', 'arch/ppc/arch_jmp_storage.h'],
             inputs=['tools/ppc/test_setjmp.py'])
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'arch/ppc/setjmp.S'
HEADER = ROOT / 'arch/ppc/arch_jmp_storage.h'


def check(source, header):
    offsets = {'R1': 0, 'R2': 4, 'R13': 8, 'LR': 84, 'CR': 88,
               'IRQ_DEPTH': 92, 'EXCEPTION_DEPTH': 96}
    for name, value in offsets.items():
        if not re.search(r'^#define PPC_JMP_' + name + r'_OFFSET\s+' + str(value) + r'$', header, re.M):
            return False
    if not re.search(r'^#define ARCH_JMP_BYTES\s+100$', header, re.M):
        return False
    expected = '''stw 1,PPC_JMP_R1_OFFSET(3)
    stw 2,PPC_JMP_R2_OFFSET(3)
    stmw 13,PPC_JMP_R13_OFFSET(3)
    mflr 0
    stw 0,PPC_JMP_LR_OFFSET(3)
    mfcr 0
    stw 0,PPC_JMP_CR_OFFSET(3)
    lis 4,kctx_irq_depth@ha
    lwz 0,kctx_irq_depth@l(4)
    stw 0,PPC_JMP_IRQ_DEPTH_OFFSET(3)
    lis 4,kctx_exc_depth@ha
    lwz 0,kctx_exc_depth@l(4)
    stw 0,PPC_JMP_EXCEPTION_DEPTH_OFFSET(3)
    li 3,0
    blr
    lwz 0,PPC_JMP_IRQ_DEPTH_OFFSET(3)
    lis 4,kctx_irq_depth@ha
    stw 0,kctx_irq_depth@l(4)
    lwz 0,PPC_JMP_EXCEPTION_DEPTH_OFFSET(3)
    lis 4,kctx_exc_depth@ha
    stw 0,kctx_exc_depth@l(4)
    lwz 0,PPC_JMP_LR_OFFSET(3)
    mtlr 0
    lwz 0,PPC_JMP_CR_OFFSET(3)
    mtcr 0
    lmw 13,PPC_JMP_R13_OFFSET(3)
    lwz 2,PPC_JMP_R2_OFFSET(3)
    lwz 1,PPC_JMP_R1_OFFSET(3)
    li 3,1
    blr'''
    instructions = [line.strip() for line in source.splitlines()
                    if line.startswith('    ') and not line.strip().startswith('.')]
    return instructions == [line.strip() for line in expected.splitlines()]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source, header = SOURCE.read_text(), HEADER.read_text()
    assert check(source, header), 'jump context mismatch'
    if args.mutate:
        anchor = '    stw 0,PPC_JMP_EXCEPTION_DEPTH_OFFSET(3)\n'
        assert source.count(anchor) == 1
        assert not check(source.replace(anchor, '', 1), header), 'mutant survived'
        print('RED: exception-depth save removed (1/1)')
    print('PASS: PPC jump context')


if __name__ == '__main__':
    main()
