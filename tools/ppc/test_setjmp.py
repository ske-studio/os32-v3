#!/usr/bin/env python3
"""PPC shell entry §5. Run the real jump on qemu-user with a modeled MSR."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'arch/ppc/setjmp.S'

HARNESS = r'''
#include "arch_jmp_storage.h"
.bss
.balign 16
context: .space ARCH_JMP_BYTES
stack: .space 256
stack_top:
.data
.globl kctx_irq_depth, kctx_exc_depth
kctx_irq_depth: .long 2
kctx_exc_depth: .long 3
msr: .long 0xa030
busy: .long 1
.text
.globl _start
_start:
    lis 3,context@ha
    addi 3,3,context@l
    bl exec_setjmp
    cmpwi 3,0
    bne landed
    lis 3,context@ha
    addi 3,3,context@l
    lwz 5,PPC_JMP_BUSY_OFFSET(3)
    cmpwi 5,0
    bne failed
    lis 4,busy@ha
    addi 4,4,busy@l
    stw 4,PPC_JMP_BUSY_OFFSET(3)
    li 5,9
    lis 4,kctx_irq_depth@ha
    stw 5,kctx_irq_depth@l(4)
    lis 4,kctx_exc_depth@ha
    stw 5,kctx_exc_depth@l(4)
    li 5,0x32
    ori 5,5,0x8000
    lis 4,msr@ha
    stw 5,msr@l(4)
    lis 1,stack_top@ha
    addi 1,1,stack_top@l
    b exec_longjmp
landed:
    cmpwi 3,1
    bne failed
    lis 4,context@ha
    addi 4,4,context@l
    lwz 5,PPC_JMP_R1_OFFSET(4)
    cmpw 5,1
    bne failed
    lwz 5,PPC_JMP_BUSY_OFFSET(4)
    cmpwi 5,0
    bne failed
    lis 4,busy@ha
    lwz 5,busy@l(4)
    cmpwi 5,0
    bne failed
    lis 4,msr@ha
    lwz 5,msr@l(4)
    cmpwi 5,0x2032
    bne failed
    lis 4,kctx_irq_depth@ha
    lwz 5,kctx_irq_depth@l(4)
    cmpwi 5,2
    bne failed
    lis 4,kctx_exc_depth@ha
    lwz 5,kctx_exc_depth@l(4)
    li 3,23
    cmpwi 5,3
    bne finish
    li 3,0
    b finish
failed:
    li 3,1
finish:
    li 0,1
    sc
'''


def run(source, folder):
    # Linux user mode cannot read or write the privileged MSR. Keep the
    # product mask/merge, register/stack transfers and busy store executable.
    source = re.sub(r'^    mfmsr (\d+)$', r'    lis 12,msr@ha\n    lwz \1,msr@l(12)',
                    source, flags=re.M)
    source = re.sub(r'^    mtmsr (\d+)$', r'    lis 12,msr@ha\n    stw \1,msr@l(12)',
                    source, flags=re.M)
    path = folder / 'jump.S'
    path.write_text(source + HARNESS)
    cc = Path(os.environ['PPC_CROSS_DIR']) / 'bin/powerpc-elf-gcc'
    binary = folder / 'jump'
    subprocess.run([cc, '-nostdlib', '-nostdinc', '-static', '-Iarch/ppc',
                    '-Wl,-e,_start', path, '-o', binary], cwd=ROOT, check=True)
    return subprocess.run(['qemu-ppc', binary], timeout=5).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory() as td:
        folder = Path(td)
        assert run(source, folder) == 0
        if args.mutate:
            anchor = '    stw 0,PPC_JMP_EXCEPTION_DEPTH_OFFSET(3)\n'
            assert source.count(anchor) == 1
            assert run(source.replace(anchor, ''), folder) == 23
            print('RED: exception-depth save removed (1/1)')
    print('PASS: PPC jump context')


if __name__ == '__main__':
    main()
