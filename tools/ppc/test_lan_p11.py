#!/usr/bin/env python3
"""Exercise PPC DMA ownership transitions and inspect emitted instructions."""
import argparse
import re
import subprocess
import tempfile
from pathlib import Path

CHECK = dict(name="ppc-lan-p11", tier=2, budget=60,
             covers=["arch/ppc/dma_cache.c", "tools/ppc/test_lan_p11.py",
                     "tools/ppc/lan_p11_fake.c"],
             inputs=["drivers/net/gem/gem.h", "include/types.h", "include/callconv.h",
                     "arch/ppc/ppc_oea.h", "arch/ppc/ppc_boot.h", "build/ppc-kboot.mk"])
ROOT = Path(__file__).resolve().parents[2]
PPC = Path('/home/hight/opt/cross-ppc/bin')
FLAGS = '''-std=gnu11 -mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec
-msdata=none -ffreestanding -fno-pie -fno-stack-protector -nostdlib -fcommon
-fsigned-char -fno-short-enums -fno-builtin -fno-asynchronous-unwind-tables -O2
-Wall -Wextra -Werror -Werror=vla -Werror=implicit-int
-Werror=implicit-function-declaration -ffunction-sections -fdata-sections
-D__cdecl= -D__KERNEL_BUILD__ -DKBOOT_NEGATIVE=0 -MMD -MP'''.split()
INCLUDES = ['-I.', '-Iinclude', '-Iarch/ppc', '-Iplatform/powermac', '-Isdk/include',
            '-Isdk/include/os32', '-Ikernel', '-Iinit', '-Ikernel/console',
            '-Ikernel/irq', '-Ikernel/time', '-Imm', '-Ilib', '-Ibuild/out/ppc-kboot']
MUTATIONS = [
    ('sync', '    ppc_sync();', '    if (op != DMA_PREWRITE) ppc_sync();', 'H:A4 dma-unpublished', 'host'),
    ('write', '                ppc_dcbst_line((void *)line);', '                ppc_dcbf_line((void *)line);', 'H:A4 dma-unpublished', 'host'),
    ('end', 'last = (end - 1) &', 'last = (end - 1 - PPC_CACHE_LINE) &', 'H:A4 dma-unpublished', 'host'),
    ('boundary', 'if (op == DMA_POSTREAD && ((start | end) & (PPC_CACHE_LINE - 1)))', 'if (0)', 'H:boundary', 'host'),
    ('instruction', '"dcbst 0,%0"', '"dcbf 0,%0"', 'H:instruction ppc_dcbst_line', 'object'),
]


def execute(cmd):
    return subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def check(source, temp, kind):
    if kind == 'host':
        exe = temp / 'test'
        built = execute(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-DPPC_DMA_TEST',
                         '-I.', '-Iinclude', '-Iarch/ppc', str(source),
                         'tools/ppc/lan_p11_fake.c', '-o', str(exe)])
        if built.returncode:
            raise RuntimeError('host compile failed: ' + built.stdout)
        return execute([str(exe)])
    obj = temp / 'dma.o'
    built = execute([str(PPC / 'powerpc-elf-gcc'), *FLAGS, *INCLUDES, '-c', str(source), '-o', str(obj)])
    if built.returncode:
        raise RuntimeError('PPC compile failed: ' + built.stdout)
    dis = execute([str(PPC / 'powerpc-elf-objdump'), '-d', str(obj)])
    if dis.returncode:
        raise RuntimeError('objdump failed: ' + dis.stdout)
    result = 'REACHED object\n'
    for name, insn in [('ppc_dcbst_line', 'dcbst'), ('ppc_dcbf_line', 'dcbf'),
                       ('ppc_dcbi_line', 'dcbi'), ('ppc_sync', 'hwsync')]:
        match = re.search(r'<%s>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)' % name, dis.stdout, re.S)
        if not match or not re.search(r'\b%s\b' % insn, match.group(1)):
            return subprocess.CompletedProcess([], 1, result + 'H:instruction ' + name + '\n')
    return subprocess.CompletedProcess([], 0, result)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'arch/ppc/dma_cache.c').read_text()
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        source = temp / 'dma_cache.c'
        source.write_text(original)
        for kind in ('host', 'object'):
            result = check(source, temp, kind)
            if result.returncode:
                raise RuntimeError('baseline failed: ' + result.stdout)
            print(result.stdout, end='')
        if args.mutate:
            for name, old, new, diagnostic, kind in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError('mutation anchor is not unique: ' + name)
                source.write_text(original.replace(old, new, 1))
                result = check(source, temp, kind)
                if result.returncode != 1 or diagnostic not in result.stdout.splitlines() or ('REACHED ' + kind) not in result.stdout.splitlines():
                    raise RuntimeError('mutation failed: ' + name + '\n' + result.stdout)
                print('RED ' + name + ': ' + diagnostic)
    print('PASS ppc-lan-p11')


if __name__ == '__main__':
    main()
