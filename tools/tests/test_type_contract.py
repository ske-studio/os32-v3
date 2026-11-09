#!/usr/bin/env python3
"""H1: isolated type contract, real product flags, and libc-free ILP32 runtime."""
CHECK = dict(name='type-contract', tier=1, needs=[], budget=15,
    covers=['include/type_contract.h', 'include/types.h', 'include/callconv.h',
            'arch/x86/arch_type_contract.h', 'tools/abi/type_contract.c',
            'tools/tests/test_type_contract.py', 'tools/tests/host32.py',
            'tools/build_compare.py', 'tools/gen_build_id.py',
            'Makefile', 'build/**/*.mk', 'sdk/include/os32/**'])
import argparse
import hashlib
import os
import re
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
    names = ['CC', 'KERNEL_CFLAGS', 'PROGRAM_FLAGS', 'CFLAGS_SQLITE', 'SQLITE_SA_CFLAGS', 'CFLAGS_BOOT']
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


# Registered C recipes and their flag-variable paths. Updating a recipe requires
# reviewing its profile and updating this inventory. Assembly is deliberately
# absent; host tools and the separate PPC bootstrap have their own profiles.
def recipe_inventory(root):
    inventory = {}
    for path in [root / 'Makefile', *sorted((root / 'build').glob('*.mk')),
                 *sorted((root / 'build/arch').glob('*.mk'))]:
        statements = path.read_text().replace('\\\n', ' ').splitlines()
        selected = []
        rule = ''
        for line in statements:
            code = line.split('#', 1)[0].rstrip()
            if not code.strip():
                continue
            if not line.startswith('\t'):
                if ':' in code and not re.search(r'[:+?]?=', code):
                    rule = code
                # Include definitions and target-specific overrides in the
                # route inventory, not just the final compiler invocation.
                if re.search(r'\b\w*FLAGS\w*\s*[:+?]?=', code):
                    selected.append(code)
            elif re.search(r'\$+\(\w*CC\)|\b(?:gcc|clang|cc)\b|(?:^|\s)-c(?:\s|$)|\.c(?:\s|$|\))', code):
                if re.search(r'\.S(?:\s|$)', rule):
                    continue
                selected.extend([rule, code])
            abi = re.findall(r'-f(?:unsigned-char|signed-char|(?:no-)?short-enums)\b', code)
            if abi:
                assert str(path.relative_to(root)) in ('build/arch/x86.mk', 'build/arch/ppc.mk'), (path, code)
                assert abi == ['-fsigned-char', '-fno-short-enums'], (path, abi)
        if selected:
            inventory[str(path.relative_to(root))] = hashlib.sha256(
                '\n'.join(selected).encode()).hexdigest()
    return inventory


RECIPE_PROFILES = {'Makefile': '8b0e41b187ce90447948607d960f9fc6658f2ff621c1bdd6e301eb502acc0b9b',
 'build/abi_contract.mk': '0c7367f0732414fdfe057e2206beabe904676c92b25a5e4d4c618765a0b3bd17',
 'build/arch/ppc.mk': 'd67ef72bbbebfe34f6b12cf10d7c6fc48f039334624c64c17804d07cb1c4d4c6',
 'build/arch/x86.mk': 'b9f163057799ddb2b0f8606ed30082ca65663ce5ddfef140fcfde8be92362401',
 'build/assets.mk': '3d7aba6be6f1214f44d30136be43a587a5bd33af6fd8277d07b4955cc8ad6126',
 'build/boot.mk': 'ea4e883a4eb74788de1428b698526d2ced7584aa2a6d79a7ba8042b12d9d8e5b',
 'build/cd-module.mk': 'd59fe100247318a14c863f676fcf6d29561ea3cc3462dbe847fd06db8d8b8f1b',
 'build/cirrus-module.mk': '2b24fdc61c6564c363e0f01cbcc5757af7775d4cd7046f241cbc8f0c36bea4dc',
 'build/config.mk': '5599a615b1e5745f938310b913a6a4095e079ed621c6071feb415d7a4d6337fc',
 'build/fep-module.mk': 'e52a293aad1c9173c3b6ad5c6d4ecbdba8445f27bf3300cdf55c3b324c0838c0',
 'build/fm-module.mk': 'bb73bc899c1ec30ae76a980dc6ee704062a66be8bd6459dcfbab7552296756a8',
 'build/geforce-module.mk': '5297680d075354eb6e4c803001fb055c615cc48c7d2fcc84dbda0763ee1bd57f',
 'build/hwdev.mk': '3818a44cf7efc3a0f69424aa7599693f1f732e5d7d8cd86b5be8019a52ed06b2',
 'build/kcg-module.mk': 'e85942d2b0822b6d7e911f0d12b95af6ddf1299ad3c8a19900a5d16bbd491bd8',
 'build/kdebug.mk': 'dbfd5a77c4570173810e2c677ac5383e4d1989f6dc81d70d33b90a429b9768fc',
 'build/kernel-objects.mk': 'e37f61a5cac3ddcdd91005861f119a3428bd4dc902af50ced07b52fab8125802',
 'build/kernel.mk': '5a6b019d769dec451d381bfea9d6f1073b8c2f2b5dd79a29c4820c8a244fb02a',
 'build/lan-module.mk': '621a519487ded13c39734a5a3b4c45e1b162a2ee619a38802455fc6d3a4bf4cd',
 'build/libs.mk': '75e8dcf8a89cfc54eaee9a6a4a9401891531d96b2dd3325d77a4032baefd61d6',
 'build/pcm-module.mk': '8b809c9c177197d5537336230358802da86e4aad05c779098b3d133b6f5ec652',
 'build/ppc.mk': '4faed6210dab914e1b492181b9bf50c8460d3cc159e3654ffc4922aff62210e0',
 'build/programs.mk': '88b0176bcaab397e061d242fdb0132ec67e98702afcdc429a35ff192ef1108d8',
 'build/sqlite-proto.mk': '61603231705c1ef6ef63df88296b6595f43af03c23b6ae47dc87ef78cdfbba7d',
 'build/trident-module.mk': '06a322a08eb2ac32b35454c0bbaaee29b77c69590e19b6245a1220976986c942',
 'build/v86-module.mk': 'd29c5ac3199b89efd595b1858ee3c4dc4007d1cc9c314ebe80c5491c77b9c1a9'}


def check_recipes(root):
    actual = recipe_inventory(root)
    assert actual == RECIPE_PROFILES, ('unregistered C recipe or flags route',
        [name for name in actual.keys() | RECIPE_PROFILES.keys()
         if actual.get(name) != RECIPE_PROFILES.get(name)])


def recipe_mutations(directory):
    tree = directory / 'recipes'
    tree.mkdir()
    shutil.copy2(ROOT / 'Makefile', tree / 'Makefile')
    shutil.copytree(ROOT / 'build', tree / 'build',
                    ignore=shutil.ignore_patterns('out', 'sdk'))
    cases = [('build/programs.mk',
              '$(CC) $(PROGRAM_FLAGS) -DOS32_CRT_RESIDENT -c sdk/allocator/nano_adapter.c -o $@',
              '$(CC) $(PROGRAM_FLAGS) -funsigned-char -DOS32_CRT_RESIDENT -c sdk/allocator/nano_adapter.c -o $@'),
             ('build/config.mk',
              'CFLAGS_COMMON = $(C_STD) $(C_DIALECT_ERRORS) $(CFLAGS_MACHINE)',
              'CFLAGS_COMMON = $(C_STD) $(C_DIALECT_ERRORS) $(CFLAGS_MACHINE) -fshort-enums'),
             ('build/config.mk', 'CFLAGS_BASE = $(KERNEL_CFLAGS)', 'CFLAGS_BASE = -m32'),
             ('Makefile', None, '\nunregistered.o: unregistered.c\n\t$(CC) -m32 -c $< -o $@\n'),
             ('Makefile', None, '\nalias.o: alias.c\n\t$(OTHER_COMPILER) -c $< -o $@\n')]
    for name, anchor, replacement in cases:
        path = tree / name
        original = path.read_text()
        if anchor is not None:
            assert original.count(anchor) == 1, anchor
        path.write_text(original + replacement if anchor is None else
                        original.replace(anchor, replacement, 1))
        try:
            check_recipes(tree)
        except AssertionError:
            print('RED C recipe/flags inventory: ' + replacement)
        else:
            raise AssertionError('surviving recipe mutation: ' + replacement)
        finally:
            path.write_text(original)


def probe_artifacts(directory, profile, mutate):
    # Execute the actual first product recipe, with a private output directory.
    # The mutation removes syntax-only and explicitly emits an object, H5.
    import importlib.util
    spec = importlib.util.spec_from_file_location('build_compare', ROOT / 'tools/build_compare.py')
    comparator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(comparator)
    left, right = directory / 'before', directory / 'after'
    for tree in (left, right):
        for rel in comparator.artifacts(tree):
            path = tree / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'unchanged comparison fixture')
    recipe = next(line.strip() for line in (ROOT / 'build/abi_contract.mk').read_text().splitlines()
                  if line.startswith('\t$(CC) $(KERNEL_CFLAGS)'))
    query = directory / 'probe.mk'
    flags = shlex.join(profile['KERNEL_CFLAGS'])
    cc = shlex.join([str(Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc')])
    prefix = f'CC := {cc}\nKERNEL_CFLAGS := {flags}\nARCH := x86\nBUILD_OUT := {right}/build/out\nprobe:\n\t'
    query.write_text(prefix + recipe + '\n')
    result = invoke(['make', '-s', '-f', str(query), 'probe'])
    assert result.returncode == 0, result.stderr
    assert comparator.compare(left, right) == []
    assert not list(right.rglob('*.d'))
    print('PASS actual build probe produces no object or dependency file')
    if mutate:
        assert recipe.count('-fsyntax-only') == 1
        query.write_text(prefix + recipe.replace('-fsyntax-only',
                         '-c -o $(BUILD_OUT)/abi-mutant.o') + '\n')
        result = invoke(['make', '-s', '-f', str(query), 'probe'])
        assert result.returncode == 0, result.stderr
        assert comparator.compare(left, right) == ['build/out/abi-mutant.o']
        print('RED build_compare: syntax-only removed, extra object detected')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--ppc', action='store_true', help='manual/PM only; never used by CHECK')
    args = parser.parse_args()
    check_recipes(ROOT)
    print('PASS registered C recipes and flag-variable paths')
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
            probe_artifacts(directory, profile, args.mutate)
            print('PASS host32 runtime: ILP32, signed char8, enum4, u64=8/4, LE')
        if args.mutate:
            recipe_mutations(directory)
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
