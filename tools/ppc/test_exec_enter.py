#!/usr/bin/env python3
"""Check the assembled PPC USER transition and its MSR mutation."""
CHECK = dict(name='ppc-exec-enter', tier=1,
             covers=['arch/ppc/exec_enter.S', 'tools/ppc/test_exec_enter.py'],
             inputs=['arch/ppc/ppc_oea.h', 'build/ppc-kboot.mk'], needs=[], budget=3)
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CROSS = Path('/home/hight/opt/cross-ppc/bin')
SOURCE = ROOT / 'arch/ppc/exec_enter.S'


def inspect(source):
    with tempfile.TemporaryDirectory(prefix='ppc-enter-') as directory:
        tmp = Path(directory)
        asm = tmp / 'entry.S'
        obj = tmp / 'entry.o'
        asm.write_text(source)
        subprocess.run([str(CROSS / 'powerpc-elf-gcc'), '-mcpu=7450', '-m32',
                        '-mbig-endian', '-I' + str(ROOT / 'arch/ppc'), '-c',
                        str(asm), '-o', str(obj)], check=True)
        output = subprocess.check_output([str(CROSS / 'powerpc-elf-objdump'),
                                          '-dr', str(obj)], text=True)
    lines = output.splitlines()
    instructions = [line.split('\t')[-1].strip() for line in lines
                    if re.match(r'^\s*[0-9a-f]+:\s+[0-9a-f]{2} ', line)]
    joined = '\n'.join(instructions)
    if not re.search(r'stw\s+r?1,0\(r?3\)', joined) or 'ppc_exec_kernel_sp' not in output:
        raise RuntimeError('kernel stack store missing')
    for operand in ('7,0', '1,4', '3,8', '4,12', '5,16', '6,20'):
        if not re.search(r'lwz\s+r?' + operand.replace(',', r',\s*') + r'\(r?30\)', joined):
            raise RuntimeError('entry load missing: ' + operand)
    srr0 = next(i for i, line in enumerate(instructions) if line.startswith('mtsrr0'))
    lr_clear = next((i for i, line in enumerate(instructions[:srr0])
                     if re.fullmatch(r'li\s+r?0,\s*0', line)
                     and i + 1 < srr0
                     and re.fullmatch(r'mtlr\s+r?0', instructions[i + 1])), None)
    if lr_clear is None or any(re.match(r'bl(?:\s|$)', line)
                               for line in instructions[lr_clear + 2:srr0]):
        raise RuntimeError('LR not cleared after last call')
    if not instructions[srr0 + 1].startswith('mtsrr1') or instructions[srr0 + 2] != 'rfi':
        raise RuntimeError('SRR transition sequence incorrect')
    match = re.search(r'ori\s+r?8,r?8,(\d+)', joined)
    if not match:
        raise RuntimeError('MSR immediate missing')
    return int(match.group(1)) & 0xffff


def main():
    source = SOURCE.read_text()
    if inspect(source) != 0xd032:
        print('FAIL: USER MSR')
        return 1
    if '--mutate' in sys.argv:
        old = 'PPC_MSR_EE|PPC_MSR_PR|PPC_MSR_ME|PPC_MSR_IR'
        assert source.count(old) == 1
        if inspect(source.replace(old, old + '|PPC_MSR_BE')) != 0xd232:
            raise RuntimeError('mutation did not produce expected BE bit')
        print('BE bit: RED (1)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
