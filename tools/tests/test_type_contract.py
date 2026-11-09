#!/usr/bin/env python3
"""H1: isolated type contract, real product flags, and libc-free ILP32 runtime."""
CHECK = dict(name='type-contract', tier=1, needs=[], budget=15,
    covers=['include/type_contract.h', 'include/types.h', 'include/callconv.h',
            'arch/x86/arch_type_contract.h', 'tools/abi/type_contract.c',
            'tools/tests/test_type_contract.py', 'tools/tests/host32.py',
            'Makefile', 'build/**/*.mk', 'sdk/include/os32/**'])
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def invoke(argv, **kwargs):
    return subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, **kwargs)


def profiles(directory, ppc=False):
    names = ['CC', 'KERNEL_CFLAGS', 'PROGRAM_FLAGS', 'CFLAGS_SQLITE', 'SQLITE_SA_CFLAGS']
    query = directory / 'query.mk'
    values = directory / 'values'
    values.unlink(missing_ok=True)
    query.write_text('.PHONY: type-query\ntype-query:\n' + ''.join(
        f'\t$(file >>{values},{name}=$({name}))\n' for name in names) + '\t@:\n')
    env = dict(os.environ)
    for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'GNUMAKEFLAGS'):
        env.pop(name, None)
    result = invoke(['make', '-s', '--no-print-directory', '-f', 'Makefile', '-f', str(query),
                     'BUILD_OUT=' + str(directory / 'out'),
                     'ARCH=' + ('ppc' if ppc else 'x86'),
                     'PLATFORM=' + ('powermac' if ppc else 'pc98'), 'type-query'], env=env)
    assert result.returncode == 0, result.stderr
    return {key: shlex.split(value) for key, value in
            (line.split('=', 1) for line in values.read_text().splitlines())}


def syntax(cc, flags, directory, text=None):
    source = ROOT / 'tools/abi/type_contract.c'
    if text is not None:
        source = directory / 'probe.c'
        source.write_text(text)
    dep = ['-MF', str(directory / 'probe.d')] if any(f in flags for f in ('-MMD', '-MD')) else []
    result = invoke([*cc, *flags, '-I' + str(directory), '-Iinclude',
                     *dep, '-fsyntax-only', str(source)])
    return result


def runtime(directory):
    source = directory / 'runtime.c'
    source.write_text('''#include "type_contract.h"
static int observe(void) {
    union { unsigned long n; unsigned char b[4]; } order = { 0x12345678UL };
    return sizeof(int)!=4 || sizeof(long)!=4 || sizeof(void *)!=4 ||
        sizeof(short)!=2 || sizeof(unsigned long long)!=8 ||
        sizeof(enum type_contract_enum)!=4 || (char)-1>=0 || __CHAR_BIT__!=8 ||
        sizeof(u8)!=1 || (u8)-1==0 || sizeof(i8)!=1 || (i8)-1>=0 ||
        sizeof(u16)!=2 || sizeof(i16)!=2 || (i16)-1>=0 ||
        sizeof(u32)!=4 || sizeof(i32)!=4 || (i32)-1>=0 || sizeof(uptr)!=4 ||
        __builtin_offsetof(struct type_contract_u16,n)!=2 ||
        __builtin_offsetof(struct type_contract_u32,n)!=4 ||
        __builtin_offsetof(struct type_contract_u64,n)!=4 || order.b[0]!=0x78;
}
void _start(void) {
    int code = observe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
''')
    exe = directory / 'runtime'
    host32.build(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie',
                  '-no-pie', '-nostdlib', '-static', '-I' + str(directory),
                  '-Iinclude', str(source), '-o', str(exe)], cwd=ROOT,
                 check=True, capture_output=True, text=True)
    assert host32.run([str(exe)]).returncode == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--ppc', action='store_true', help='manual/PM only; never used by CHECK')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='type-contract-') as temporary:
        directory = Path(temporary)
        arch = 'ppc' if args.ppc else 'x86'
        provider = (ROOT / f'arch/{arch}/arch_type_contract.h').read_text()
        contract = (ROOT / 'include/type_contract.h').read_text()
        (directory / 'arch_type_contract.h').write_text(provider)
        (directory / 'type_contract.h').write_text(contract)
        profile = profiles(directory, args.ppc)
        cc = profile['CC']
        if not args.ppc:
            cc = [str(Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc')]
        if args.ppc and not shutil.which(cc[0]):
            print('対象外: PPC toolchain unavailable (manual/PM probe)')
            return
        # SDK CRT and userland both use PROGRAM_FLAGS (build/programs.mk:30-33).
        flags = profile['KERNEL_CFLAGS']
        for label, key in [('kernel', 'KERNEL_CFLAGS'), ('userland', 'PROGRAM_FLAGS'),
                           ('SDK CRT', 'PROGRAM_FLAGS'), ('SQLite', 'CFLAGS_SQLITE')]:
            result = syntax(cc, profile[key], directory)
            assert result.returncode == 0, (label, result.stderr)
            print('PASS ' + arch + ' ' + label)
        if not args.ppc:
            result = syntax(cc, profile['SQLITE_SA_CFLAGS'], directory)
            assert result.returncode == 0, result.stderr
            for header in ('os32_kapi_shared.h', 'os32_gui_shared.h'):
                result = syntax(cc, ['-m32', '-std=gnu89', '-Werror', '-Isdk/include/os32'],
                                directory, '#include "' + header + '"\n')
                assert result.returncode == 0, result.stderr
            runtime(directory)
            print('PASS host32 runtime: ILP32, signed char8, enum4, u64=8/4, LE')
        if args.mutate:
            mutations = [('provider', '#define ARCH_U64_MEMBER_ALIGN ' + ('8' if args.ppc else '4'),
                          '#define ARCH_U64_MEMBER_ALIGN ' + ('4' if args.ppc else '8')),
                         ('contract', '(char)-1 < 0', '(char)-1 > 0'),
                         ('contract', 'sizeof(enum type_contract_enum) == 4', 'sizeof(enum type_contract_enum) == 1'),
                         ('contract', 'sizeof(long) == 4', 'sizeof(long) == 8'),
                         ('provider', '__ORDER_BIG_ENDIAN__' if args.ppc else '__ORDER_LITTLE_ENDIAN__',
                          '__ORDER_LITTLE_ENDIAN__' if args.ppc else '__ORDER_BIG_ENDIAN__')]
            for owner, anchor, replacement in mutations:
                original = provider if owner == 'provider' else contract
                assert original.count(anchor) == 1, anchor
                path = directory / ('arch_type_contract.h' if owner == 'provider' else 'type_contract.h')
                path.write_text(original.replace(anchor, replacement))
                result = syntax(cc, flags, directory)
                path.write_text(original)
                assert result.returncode != 0 and 'static assertion failed' in result.stderr, result.stderr
                print('RED product contract: ' + anchor)
            for flag in ('-funsigned-char', '-fshort-enums'):
                result = syntax(cc, [*flags, flag], directory)
                assert result.returncode != 0 and 'static assertion failed' in result.stderr
                print('RED compiler ABI drift: ' + flag)
        print('PASS ' + arch + ' type contract (u16/u32 member=2/4; u64 member=' +
              ('8; BE, compile measured' if args.ppc else '4; LE') + ')')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
