#!/usr/bin/env python3
"""Execute the public calling convention and ABI alignment on an ILP32 host."""
CHECK = dict(name='abi-boundary', tier=1, needs=[], budget=15,
    covers=['include/abi.h', 'include/cpu.h', 'include/types.h',
            'arch/x86/arch_abi.h', 'arch/ppc/arch_abi.h',
            'tools/tests/host_arch/arch_abi.h',
            'include/callconv.h', 'sdk/include/os32/**',
            'arch/x86/kernel/cpu_flags.inc',
            'tools/tests/test_abi_boundary.py', 'tools/tests/host32.py'])
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(directory, callconv, abi):
    (directory / 'callconv.h').write_text(callconv)
    (directory / 'arch_abi.h').write_text(abi)
    source = directory / 'probe.c'
    source.write_text('''#include "callconv.h"
#include "arch_abi.h"
typedef unsigned long word;
__attribute__((noinline)) word __cdecl callee(word a, word b, word c) {
    return a + b * 3 + c * 7;
}
int main(void) {
    word before, after, result;
    __asm__ volatile("movl %%esp,%0" : "=r"(before));
    __asm__ volatile("pushl $3; pushl $2; pushl $1; call callee; addl $12,%%esp"
                     : "=a"(result) : : "ecx", "edx", "memory", "cc");
    __asm__ volatile("movl %%esp,%0" : "=r"(after));
    if (before != after) {
        __asm__ volatile("movl %0,%%esp" : : "r"(before) : "memory");
        return 1;
    }
    if (result != 28 || ABI_WORD_BYTES != 4 || ABI_WORD_BITS != 32 || ABI_WORD_ALIGN != 4) return 2;
    return (0x1237UL & ~(ABI_STACK_ALIGN - 1UL)) != 0x1230UL;
}
''')
    exe = directory / 'probe'
    host32.build(['gcc', '-m32', '-std=gnu89', '-O0', '-fno-omit-frame-pointer',
                  '-fno-pie', '-no-pie', str(source), '-o', str(exe)], check=True,
                 capture_output=True, text=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    callconv = (ROOT / 'include/callconv.h').read_text()
    abi = (ROOT / 'arch/x86/arch_abi.h').read_text()
    assert '%define ABI_STACK_ALIGN 16' in (ROOT / 'arch/x86/kernel/cpu_flags.inc').read_text()
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp)
        assert observe(directory, callconv, abi) == 0
        assert observe(directory, callconv, (ROOT / "arch/ppc/arch_abi.h").read_text()) == 0
        # The public SDK remains independently usable in GNU89.
        subprocess.run(['gcc', '-m32', '-std=gnu89', '-Werror', '-fsyntax-only',
                        '-Isdk/include/os32', '-x', 'c', '-'], cwd=ROOT, check=True,
                       input='#include "os32_kapi_shared.h"\n', text=True)
        if args.mutate:
            anchor = '#define __cdecl __attribute__((cdecl))'
            assert callconv.count(anchor) == 1
            assert observe(directory, callconv.replace(anchor, '#define __cdecl __attribute__((stdcall))'), abi) != 0
            print('RED stdcall: runtime stack mismatch')
            anchor = '#define ABI_STACK_ALIGN 16'
            assert abi.count(anchor) == 1
            assert observe(directory, callconv, abi.replace(anchor, '#define ABI_STACK_ALIGN 4')) != 0
            print('RED stack alignment: runtime unaligned top')
    print('PASS public cdecl / GNU89 / ABI constants')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
