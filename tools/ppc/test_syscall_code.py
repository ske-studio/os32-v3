#!/usr/bin/env python3
"""Generate and disassemble the PPC syscall page on an ILP32 host."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

CHECK = dict(name='ppc-syscall-code', tier=2, budget=60,
             covers=['arch/ppc/syscall_code.c', 'tools/ppc/test_syscall_code.py'],
             inputs=['include/syscall_code.h', 'exec/ring3_str.h', 'exec/ring3_ls.h',
                     'arch/ppc/exec_cache.h', 'sdk/include/os32/os32_kapi_shared.h'])
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'arch/ppc/syscall_code.c'
PPC = '/home/hight/opt/cross-ppc/bin/powerpc-elf-objdump'
HARNESS = r'''
#include "syscall_code.h"
#include "ring3_str.h"
static unsigned char page[4096];
void arch_exec_publish(void *p, u32 n) { (void)p; (void)n; }
void _start(void) {
    struct syscall_layout l;
    int ok = arch_syscall_layout(KAPI_FUNC_CAPACITY, &l) &&
             arch_syscall_emit(page, 0x10000, sizeof(page), KAPI_FUNC_CAPACITY);
    if (ok) {
        __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(&l), "d"(sizeof(l)) : "memory");
        __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(page), "d"(sizeof(page)) : "memory");
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(!ok));
    __builtin_unreachable();
}
'''


def command(args):
    return subprocess.run(args, cwd=ROOT, check=True, stdout=subprocess.PIPE,
                          stderr=None, timeout=60).stdout


def inspect(source, d):
    src, harness, exe, binary = (d / n for n in ('source.c', 'host.c', 'host', 'page.bin'))
    src.write_text(source)
    harness.write_text(HARNESS)
    command(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
             '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
             '-Iinclude', '-Imm', '-Iexec', '-Iarch/ppc', '-Isdk/include/os32',
             str(src), str(harness), '-o', str(exe)])
    data = command(['qemu-i386', str(exe)])
    if len(data) != 32 + 4096:
        raise RuntimeError('host output size')
    fields = [int.from_bytes(data[i:i + 4], 'little') for i in range(0, 32, 4)]
    off, stride, count, scratch, scratch_len, ls, _, _ = fields
    page = data[32:]
    binary.write_bytes(page)
    dis = command([PPC, '-D', '-b', 'binary', '-m', 'powerpc:common', '-EB', str(binary)]).decode()
    instructions = {}
    for line in dis.splitlines():
        match = re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+){4}\s*(.*)', line)
        if match:
            instructions[int(match[1], 16)] = match[2]
    for i in range(count):
        addr = off + i * stride
        if not re.match(rf'li\s+r?0,{i}\b', instructions.get(addr, '')):
            return False
        branch = re.match(r'b\s+(?:0x)?([0-9a-f]+)', instructions.get(addr + 4, ''))
        if not branch or int(branch[1], 16) != ls + 8:
            return False
    return (re.match(r'li\s+r?3,-10\b', instructions.get(ls, '')) is not None
            and instructions.get(ls + 4, '').startswith('blr')
            and instructions.get(ls + 8, '').startswith('sc')
            and instructions.get(ls + 12, '').startswith('blr')
            and page[scratch:scratch + scratch_len] == bytes(scratch_len))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = SOURCE.read_text()
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        if not inspect(original, d):
            raise RuntimeError('instruction mismatch')
        print('PPC syscall page disassembly OK')
        if args.mutate:
            anchor = '(l.ls_off + 8u) - (off + 4u)'
            if original.count(anchor) != 1:
                raise RuntimeError('mutation anchor')
            if inspect(original.replace(anchor, '(l.ls_off + 8u) - off'), d):
                raise RuntimeError('branch mutation survived')
            print('RED branch displacement')


if __name__ == '__main__':
    main()
