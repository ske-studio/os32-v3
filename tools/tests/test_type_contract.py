#!/usr/bin/env python3
"""H1: isolated type contract, real product flags, and libc-free ILP32 runtime."""
CHECK = dict(name='type-contract', tier=1, needs=[], budget=15,
    covers=['include/type_contract.h', 'include/types.h', 'include/callconv.h',
            'arch/x86/arch_type_contract.h', 'tools/abi/type_contract.c',
            'tools/tests/test_type_contract.py', 'tools/tests/host32.py',
            'tools/build_compare.py', 'tools/gen_build_id.py',
            'Makefile', 'build/**/*.mk', 'sdk/include/os32/**',
            'kernel/module/kdebug_imports.def', 'sdk/kapi.json'])
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
                     'PLATFORM=' + ('powermac' if ppc else 'pc98'), 'CHECK_TIME_CHILD=1', 'DEPFILES=', 'type-query'], env=env)
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


# Ask make to traverse its real include graph (including expanded feature and
# target includes). Dry-run a private target: no probe, compiler or .d remake.
def product_makefiles(root):
    paths = set()
    with tempfile.TemporaryDirectory(prefix='type-includes-') as temporary:
        query = Path(temporary) / 'query.mk'
        query.write_text('$(info TYPE_INCLUDES=$(MAKEFILE_LIST))\n.PHONY: type-includes\ntype-includes: ; @:\n')
        for arch, platform in (('x86', 'pc98'), ('ppc', 'powermac')):
            result = subprocess.run(['make', '-r', '-n', '-s', '-f', 'Makefile', '-f', str(query),
                'ARCH=' + arch, 'PLATFORM=' + platform,
                'BUILD_OUT=' + str(Path(temporary) / arch), 'DEPFILES=',
                'CHECK_TIME_CHILD=1', 'type-includes'], cwd=root,
                capture_output=True, text=True, env=dict(os.environ, MAKEFLAGS='', MFLAGS=''))
            if result.returncode:
                raise RuntimeError(result.stderr)
            for line in result.stdout.splitlines():
                if line.startswith('TYPE_INCLUDES='):
                    paths.update(name for name in line.split('=', 1)[1].split()
                                 if name == 'Makefile' or name.startswith('build/') and name.endswith('.mk'))
    return [root / name for name in sorted(paths)]


def flag_route(code):
    # Only machine/language flags and variable flow affect this contract.
    # Link flags, output paths, source/header prerequisites and echo are absent.
    return re.findall(r'\$+\([A-Za-z_][A-Za-z_0-9]*(?:\$+\(\d\))?\)|'
                      r'(?<!\S)-(?:f[\w=-]+|m[\w=-]+|std=[\w]+)', code)


def recipe_inventory(root):
    inventory = {}
    definitions = []
    used = set()
    for path in product_makefiles(root):
        selected = []
        rule = ''
        assembly = False
        for line in path.read_text().replace('\\\n', ' ').splitlines():
            code = line.split('#', 1)[0].rstrip()
            if not code.strip():
                continue
            abi = re.findall(r'-f(?:unsigned-char|signed-char|(?:no-)?short-enums)\b', code)
            if abi:
                assert str(path.relative_to(root)) in ('build/arch/x86.mk', 'build/arch/ppc.mk'), (path, code)
                assert abi == ['-fsigned-char', '-fno-short-enums'], (path, abi)
            if not line.startswith('\t'):
                assignment = re.search(r'([\w]+)\s*([:+?]?=)(.*)', code)
                if assignment:
                    definitions.append((str(path.relative_to(root)), *assignment.groups()))
                elif ':' in code:
                    rule = code.split(':', 1)[0]
                    assembly = bool(re.search(r'\.S(?:\s|$)', code))
            elif re.match(r'\s*[@+-]?(?:echo|printf)\s', code) and not re.search(r'[;&|]', code):
                continue
            elif (re.search(r'\$+\(\w*CC\)|(?:^|\s)(?:gcc|clang|cc)\b', code)
                  or re.search(r'(?:^|\s)-c(?:\s|$)', code)):
                # Compiler links and assembly do not establish C profiles.
                if '-c ' not in code and '-fsyntax-only' not in code and not re.search(r'\.c(?:\s|$)', code):
                    continue
                if assembly:
                    continue
                route = flag_route(code)
                selected.append((rule, route))
                used.update(re.findall(r'\$+\((\w+)\)', ' '.join(route)))
        if selected:
            inventory[str(path.relative_to(root))] = selected
    # Follow aliases too: an innocently named intermediate can carry ABI flags.
    while True:
        before = set(used)
        for _, name, _, value in definitions:
            if name in used:
                used.update(re.findall(r'\$+\((\w+)\)', value))
        if used == before:
            break
    for path, name, operator, value in definitions:
        if name in used:
            inventory.setdefault(path, []).append((name, operator, flag_route(value)))
    return {path: hashlib.sha256(repr(rows).encode()).hexdigest()
            for path, rows in inventory.items()}


RECIPE_PROFILES = {'Makefile': 'daec875e72bc1698c46c7ed8591f97ff72b505097dcfef0dc2d4d1e4227f737a',
 'build/abi_contract.mk': '7eac23176472be9d1e9806008811a3d82069eb5fa34ddccaee685c3c0335064e',
 'build/arch/ppc.mk': 'ed6f14d2b2629335b3f6ed9581ad152a1c406b933a42e383c0e83af8ef24eddc',
 'build/arch/x86-sources.mk': 'b67ae8dfb5a633489d2944a497a07b8a684b9c15bc07fab5c6aef7c0e8373bcd',
 'build/arch/x86.mk': '7f6e4440e7c71f5f83b5efcb9a017463ed322112c619a738b7e1718bf282c7fc',
 'build/boot.mk': 'ce33c306fe7539071e40d5db46b696661a2b65c7ac5e6748678b736e80b83b9b',
 'build/cd-module.mk': '4412ffc63a54348a987a7f4fa764f27c5bed03aca438d0efabd48f6b0545da0f',
 'build/cirrus-module.mk': 'a6c3fafc799f014728789156c35e4b51974c15f87f0f87ee6128215d3c6363fe',
 'build/config.mk': '89f773efdb1c3a5e2845eabdfaf6209e1744a9d439654fbbafabcbb4f69d9480',
 'build/features/cirrus.mk': 'b9b5f5360b2d444271885133251ad903357ca093a886884cbd3a777e99a57afd',
 'build/features/fep.mk': 'b9b5f5360b2d444271885133251ad903357ca093a886884cbd3a777e99a57afd',
 'build/features/fm.mk': 'b9b5f5360b2d444271885133251ad903357ca093a886884cbd3a777e99a57afd',
 'build/features/geforce.mk': 'b9b5f5360b2d444271885133251ad903357ca093a886884cbd3a777e99a57afd',
 'build/features/kcg.mk': 'b9b5f5360b2d444271885133251ad903357ca093a886884cbd3a777e99a57afd',
 'build/features/lan.mk': 'b9b5f5360b2d444271885133251ad903357ca093a886884cbd3a777e99a57afd',
 'build/features/pc98-devices.mk': '56f4cbe403143fc7b4618df585aa084f10f87dfeed5568a8a7d172e5fb334f9a',
 'build/features/pcm.mk': 'bedfb2f42b1f83ca4ccc01f34d5109b2b71498833c2c5efa6828a4600797c31e',
 'build/features/v86.mk': 'b9b5f5360b2d444271885133251ad903357ca093a886884cbd3a777e99a57afd',
 'build/fep-module.mk': '6d1a1345b11a85eefb952b3bf05526a885fd35292aeacdbeecc9e472c84f5aa6',
 'build/fm-module.mk': '7647aab48a2c6dba2d17e2ec63d7cdcbfbd1ced2ea3ecac1d9055b541214c0ba',
 'build/geforce-module.mk': 'e8bd8dd6f95629f5664e2a00f1eef656a865a22272d257ed467ca4aca5b595cf',
 'build/hwdev.mk': '17edf4ca501cf2dc12c3ef95341eeb19acb71b00ee465159452aa2faaecbb39d',
 'build/kcg-module.mk': 'bfcd8555fe27de7c15089e39937ce7970cf6744d7ba5886ad2e58c9b0bef92e2',
 'build/kdebug.mk': '1b2dfeb18c3abd5d5a71fda0b36bc7e45eb916cf3c3a7dac13a4e71205194556',
 'build/kernel-objects.mk': 'a007816f1f3fe2b055e39f82dcf8049cada67c98df97a0ed6546d977b197389b',
 'build/kernel.mk': '8e4cda94dce65bdb48326058ce03eef6773dc87956564dd70edff20aa770b4d0',
 'build/lan-module.mk': '08799233be7f963310118111114478955850e120273077723c984e10eb0f8bba',
 'build/libs.mk': 'af746ba6a9a46849720e32ee84d1b6807f2c97805553d4c61789e62674993145',
 'build/pcm-module.mk': '2ad322e1765093f6a5478e039df2cf6fc8acb130ca6d8f51ecd60898644e5fca',
 'build/programs.mk': '1c83c7fc4845f5f3e25e41e0f6952d290179b557fadac429ae567926fa6b4239',
 'build/select.mk': 'a289eec82335a4fe7dc9fc8399bde51a6537e4170cbced676e24f16ca2c9653e',
 'build/sources/common.mk': 'd039d275e36cf7f6876f795414884260de97479c4f080ee3d454b2999321ec96',
 'build/sources/order.mk': '9721d3d980d95290edf5c7271b6d3d239039204f3efe433f7a1b1af3e0fbfca5',
 'build/sqlite-proto.mk': '8e94483318166c3831c0def3e9c5aedd16ca2c84440261d7d6f723b524fe9a24',
 'build/trident-module.mk': '039c2f8fcc2eb5885a81c6a801bac0c79d39718c6cbcc0183380640bb89464a5',
 'build/v86-module.mk': '77314d6d3af175617057a7062a4c0cf40df1c2c88ad7be34d32a07d7df95ba00'}


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
    for name in ('sdk/kapi.json', 'kernel/module/kdebug_imports.def'):
        target = tree / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    check_recipes(tree)
    cases = [('build/programs.mk',
              '$(CC) $(PROGRAM_FLAGS) -DOS32_CRT_RESIDENT -c sdk/allocator/nano_adapter.c -o $@',
              '$(CC) $(PROGRAM_FLAGS) -funsigned-char -DOS32_CRT_RESIDENT -c sdk/allocator/nano_adapter.c -o $@'),
             ('build/config.mk',
              'CFLAGS_COMMON = $(C_STD) $(C_DIALECT_ERRORS) $(CFLAGS_MACHINE)',
              'CFLAGS_COMMON = $(C_STD) $(C_DIALECT_ERRORS) $(CFLAGS_MACHINE) -fshort-enums'),
             ('build/config.mk', 'CFLAGS_BASE = $(KERNEL_CFLAGS)', 'CFLAGS_BASE = -m32'),
             ('Makefile', None, '\nunregistered.o: unregistered.c\n\t$(CC) -m32 -c $< -o $@\n'),
             ('Makefile', None, '\nalias.o: alias.c\n\t$(OTHER_COMPILER) -c $< -o $@\n'),
             ('build/targets/x86-pc98.mk', None, '\nPROGRAM_FLAGS += -funsigned-char\n'),
             ('build/targets/x86-pc98.mk', None, '\nunregistered.o: unregistered.c\n\t$(CC) $(PROGRAM_FLAGS) -c $< -o $@\n')]
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

    # Integration edits unrelated to the C ABI must remain accepted.
    for name, addition in (
        ('build/targets/x86-pc98.mk', '\nPROGRAM_LDFLAGS += -Map=harmless.map\n'),
        ('build/programs.mk', '\nuserland/cmds/cfg.o: extra_header.h\n'),
        ('build/programs.mk', '\ninnocent-echo:\n\t@echo $(CC) $(PROGRAM_FLAGS) -c harmless.c\n')):
        path = tree / name
        original = path.read_text()
        path.write_text(original + addition)
        try:
            check_recipes(tree)
            print('PASS unrelated edit: ' + addition.strip())
        finally:
            path.write_text(original)


def direct_objects(directory):
    targets = ['userland/cmds/cfg.o', 'sdk/crt/help.o', 'userland/cmds/more.o']
    # Use actual list-derived representatives for paths which vary by profile.
    query = directory / 'objects.mk'
    query.write_text('$(info TYPE_OBJECTS=$(firstword $(C_KERNEL_OBJ)) '
        '$(addprefix $(R1_OBJ)/,$(firstword $(R1_C:.c=.o))) $(firstword $(PCM_OBJS)) '
        '$(firstword $(SQLITE_PROTO_OBJS)) $(firstword $(BOOT_C_OBJ)))\n.PHONY: object-query\nobject-query: ; @:\n')
    result = invoke(['make', '-r', '-n', '-s', '-f', 'Makefile', '-f', str(query), 'CHECK_TIME_CHILD=1', 'DEPFILES=', 'object-query'])
    assert result.returncode == 0, result.stderr
    line = next(line for line in result.stdout.splitlines() if line.startswith('TYPE_OBJECTS='))
    targets = targets + line.split('=', 1)[1].split()
    assert len(targets) == 8, targets
    for target in targets:
        result = invoke(['make', '-n', '--no-print-directory', 'CHECK_TIME_CHILD=1', 'DEPFILES=', target])
        assert result.returncode == 0, (target, result.stderr)
        assert '-fsyntax-only tools/abi/type_contract.c' in result.stdout, target
        print('PASS direct target probe: ' + target)


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
            direct_objects(directory)
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
