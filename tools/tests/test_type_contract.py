#!/usr/bin/env python3
"""H1: isolated type contract, real product flags, and libc-free ILP32 runtime."""
CHECK = dict(name='type-contract', tier=1, needs=[], budget=180,
    covers=['include/type_contract.h', 'include/types.h', 'include/callconv.h',
            'arch/x86/arch_type_contract.h', 'tools/abi/type_contract.c',
            'tools/tests/test_type_contract.py', 'tools/tests/host32.py',
            'tools/build_compare.py', 'tools/gen_build_id.py',
            'Makefile', 'build/**/*.mk', 'sdk/include/os32/**',
            'kernel/module/kdebug_imports.def', 'sdk/kapi.json',
            'boot/*.c', 'drivers/block/pc98pt.c', 'lib/sqlite3/sqlite3.c',
            'userland/tests/sqlite_standalone/*.c'])
import argparse
from collections import Counter
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


# Product entry points from targets/x86-pc98.mk, kernel.mk, programs.mk,
# boot.mk, sqlite-proto.mk and geforce-module.mk. all covers the normal
# modules/SDK/libraries/programs; the remaining goals cover optional C builds.
PRODUCT_GOALS = (
    ('all', 'kernel-r1', 'sdk', 'sqlite-module-proto', 'boot-debug',
     'h5-shapes', 'h2-fixtures', 'userland/tests/h2_stack512.bin'),
    ('kernel-splash-diag', 'kernel-splash-obs', 'kernel-pcm-e4-diag',
     'kernel-tick-probe'),
    ('GF_VPLL_PROBE=1', 'gf-vpll-probe'),
    *((f'GEFORCE_ACCEPT={mode}', 'GF_ACCEPT_ID=type-contract', 'geforce-accept', 'gshell')
      for mode in ('byte-uc', 'dword-uc', 'dword-wc')),
    ('kernel-lgy98',), ('kernel-lgy98-link',), ('kernel-nolgy98',),
)
ABI_FLAGS = {'-fsigned-char', '-funsigned-char', '-fno-signed-char',
             '-fno-unsigned-char', '-fshort-enums', '-fno-short-enums'}


def recipe_tree(destination):
    # Copy the effective worktree, never share writable files with the product.
    import importlib.util
    spec = importlib.util.spec_from_file_location('build_compare', ROOT / 'tools/build_compare.py')
    comparator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(comparator)
    comparator.snapshot(ROOT, 'HEAD', destination)
    # Fonts are local prerequisites, deliberately absent from git.
    for source in (ROOT / 'assets').rglob('*.ttf'):
        target = destination / source.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def compiler_commands(output):
    # Shell tokenization handles quoted paths and compound recipe commands.
    # A compiler must be the command word, never an argument of echo/printf.
    for line in output.replace('\\\n', ' ').splitlines():
        lexer = shlex.shlex(line, posix=True, punctuation_chars=';&|()')
        lexer.whitespace_split = True
        lexer.commenters = '#'
        try:
            tokens = list(lexer)
        except ValueError:
            assert not re.search(r'i[3-6]86-elf-(?:gcc|clang)', line), line
            continue  # make's human-readable directory messages
        command = []
        for token in [*tokens, ';']:
            if token and all(c in ';&|()' for c in token):
                while command and (Path(command[0]).name in ('env', 'ccache') or
                                   re.match(r'^[A-Za-z_][A-Za-z_0-9]*=', command[0])):
                    command.pop(0)
                compiler_positions = [i for i, word in enumerate(command)
                    if re.fullmatch(r'i[3-6]86-elf-(gcc|clang)(-[0-9.]+)?', Path(word).name)]
                # This generator takes a compiler as data, like echo/printf.
                generator = (command[:2] == ['python3', 'tools/' + 'gen_hwdev_contract.py']
                             and '--cc' in command and '--flags' in command)
                if command and command[0] not in ('echo', 'printf') and not generator:
                    assert all(i == 0 for i in compiler_positions), command
                if (command and re.fullmatch(r'i[3-6]86-elf-(gcc|clang)(-[0-9.]+)?',
                                             Path(command[0]).name)
                        and any(t.endswith('.c') for t in command[1:])):
                    yield command
                command = []
            else:
                command.append(token)


def check_recipes(root, goals=PRODUCT_GOALS):
    stamp = root / 'build/out/lgy98.flags'
    saved = stamp.read_bytes() if stamp.exists() else None
    try:
        return inspect_recipes(root, goals)
    finally:
        if saved is None:
            stamp.unlink(missing_ok=True)
        else:
            stamp.write_bytes(saved)


def inspect_recipes(root, goals):
    counts = Counter()
    env = dict(os.environ, MAKEFLAGS='', MFLAGS='', MAKEOVERRIDES='', GNUMAKEFLAGS='')
    stamp = root / 'build/out/lgy98.flags'
    saved_stamp = stamp.read_bytes() if stamp.exists() else None
    for group in goals:
        setting = {'kernel-lgy98': '5', 'kernel-lgy98-link': '9',
                   'kernel-nolgy98': 'off'}.get(group[0])
        stamp.parent.mkdir(parents=True, exist_ok=True)
        if setting is not None:
            stamp.write_text(setting + '\n')
        elif saved_stamp is None:
            stamp.unlink(missing_ok=True)
        else:
            stamp.write_bytes(saved_stamp)
        # Recursive dry-runs must not interleave command lines in stdout.
        result = subprocess.run(['make', '-j1', '-n', '-B', '--no-print-directory',
            'ARCH=x86', 'PLATFORM=pc98', 'CHECK_TIME_CHILD=1', 'DEPFILES=', *group],
            cwd=root, capture_output=True, text=True, env=env, timeout=1800)
        assert result.returncode == 0, (group, result.stderr)
        found = 0
        for command in compiler_commands(result.stdout):
            found += 1
            sources = [t for t in command[1:] if t.endswith('.c')]
            output = command[command.index('-o') + 1] if '-o' in command else ''
            # These two pre-existing profiles intentionally use GCC's x86
            # defaults, rather than CFLAGS_MACHINE. Keep their scope narrow.
            boot_sources = {'boot/boot_main.c': 'boot_main.o',
                'boot/boot_debug.c': 'boot_debug.o', 'boot/ext2_mini.c': 'ext2_mini.o',
                'boot/lz4_mini.c': 'lz4_mini.o', 'boot/vk32_boot.c': 'vk32_boot.o',
                'drivers/block/pc98pt.c': 'pc98pt_boot.o'}
            boot = (len(sources) == 1 and sources[0] in boot_sources and
                    output == 'boot/' + boot_sources[sources[0]])
            standalone_sources = {'lib/sqlite3/sqlite3.c': 'sqlite3_user.o',
                'userland/tests/sqlite_standalone/sqlite_user_vfs.c': 'sqlite_user_vfs.o',
                'userland/tests/sqlite_standalone/sqlite_standalone.c': 'sqlite_standalone.o'}
            standalone = (len(sources) == 1 and sources[0] in standalone_sources and
                output == 'userland/tests/sqlite_standalone/' + standalone_sources[sources[0]])
            probe_defaults = (sources == ['tools/abi/type_contract.c'] and
                '-fsyntax-only' in command and
                ('-Iboot' in command or 'lib/sqlite3/os32_sqlite_config.h' in command))
            flags = [t for t in command if t in ABI_FLAGS]
            char = [t in ('-fsigned-char', '-fno-unsigned-char') for t in flags
                    if t in ('-fsigned-char', '-fno-unsigned-char',
                             '-funsigned-char', '-fno-signed-char')]
            enums = [t == '-fno-short-enums' for t in flags
                     if t in ('-fshort-enums', '-fno-short-enums')]
            assert not char or char[-1], command
            assert not enums or enums[-1], command
            if not (boot or standalone or probe_defaults):
                assert char and enums, command
            label = ('probe' if '-fsyntax-only' in command else
                     'boot' if boot else 'SQLite standalone' if standalone else
                     'SQLite module' if '-std=gnu89' in command else
                     'kernel/module' if '-D__KERNEL_BUILD__' in command else 'userland/SDK')
            counts[label] += 1
        assert found, ('no C compiler calls', group)
    print('PASS expanded C calls: ' + str(sum(counts.values())) + ' ' + str(dict(counts)))
    return counts


def recipe_mutations(tree):
    cases = [('build/programs.mk',
              '$(CC) $(PROGRAM_FLAGS) -DOS32_CRT_RESIDENT -c sdk/allocator/nano_adapter.c -o $@',
              '$(CC) $(PROGRAM_FLAGS) -funsigned-char -DOS32_CRT_RESIDENT -c sdk/allocator/nano_adapter.c -o $@'),
             ('build/targets/x86-pc98.mk', None, '\nPROGRAM_FLAGS += -funsigned-char\n'),
             ('build/targets/x86-pc98.mk', None,
              '\ncontract_unregistered.elf: private PROGRAM_FLAGS := $(filter-out -fsigned-char,$(PROGRAM_FLAGS))\n'
              'all: contract_unregistered.elf\ncontract_unregistered.elf: contract_unregistered.c\n'
              '\t$(CC) $(PROGRAM_FLAGS) $< -o $@\n')]
    (tree / 'contract_unregistered.c').write_text('int main(void) { return 0; }\n')
    for name, anchor, replacement in cases:
        path = tree / name
        original = path.read_text()
        if anchor is not None:
            assert original.count(anchor) == 1, anchor
        path.write_text(original + replacement if anchor is None else
                        original.replace(anchor, replacement, 1))
        try:
            check_recipes(tree, (PRODUCT_GOALS[0],))
        except AssertionError as error:
            assert 'i386-elf-gcc' in str(error), error
            if 'contract_unregistered.elf:' in replacement:
                assert 'contract_unregistered.c' in str(error), error
            print('RED expanded C flags: ' + replacement)
        else:
            raise AssertionError('surviving recipe mutation: ' + replacement)
        finally:
            path.write_text(original)

    path = tree / 'build/programs.mk'
    original = path.read_text()
    anchor = '$(CC) $(PROGRAM_FLAGS) -DOS32_CRT_RESIDENT -c sdk/allocator/nano_adapter.c -o $@'
    assert original.count(anchor) == 1
    for suffix, accepted in (
            ('-funsigned-char -fsigned-char -fshort-enums -fno-short-enums', True),
            ('-fno-signed-char -fno-unsigned-char -fno-short-enums', True),
            ('-fsigned-char -funsigned-char', False),
            ('-fno-short-enums -fshort-enums', False),
            ('conditional', False)):
        replacement = (('if true; then ' + anchor.replace('$(PROGRAM_FLAGS)',
                       '$(PROGRAM_FLAGS) -funsigned-char') + '; fi')
                       if suffix == 'conditional' else
                       anchor.replace('$(PROGRAM_FLAGS)', '$(PROGRAM_FLAGS) ' + suffix))
        path.write_text(original.replace(anchor, replacement))
        try:
            try:
                check_recipes(tree, (PRODUCT_GOALS[0],))
            except AssertionError as error:
                assert not accepted and 'i386-elf-gcc' in str(error), error
                print('RED ' + suffix)
            else:
                assert accepted, suffix
                print('GREEN ' + suffix)
        finally:
            path.write_text(original)

    path = tree / 'build/config.mk'
    original = path.read_text()
    for goal, condition in (('kernel-lgy98', 'ifeq ($(LGY98_FLAGS),5)'),
                            ('kernel-lgy98-link', 'ifeq ($(LGY98_FLAGS),9)'),
                            ('kernel-nolgy98', 'ifndef LGY98')):
        path.write_text(original + '\n' + condition +
                        '\nKERNEL_CFLAGS += -funsigned-char\nendif\n')
        try:
            try:
                check_recipes(tree, ((goal,),))
            except AssertionError as error:
                assert 'i386-elf-gcc' in str(error), error
                print('RED stamp profile ' + goal)
            else:
                raise AssertionError('surviving LGY98 mutation: ' + goal)
        finally:
            path.write_text(original)

    # Integration edits unrelated to the C ABI must remain accepted.
    (tree / 'extra_header.h').write_text('/* harmless prerequisite */\n')
    path = tree / 'build/programs.mk'
    original = path.read_text()
    anchor = '-c sdk/allocator/nano_adapter.c -o $@'
    assert original.count(anchor) == 1
    path.write_text(original.replace(anchor, '-c sdk/allocator/nano_adapter.c -o $(CONTRACT_OUTPUT)') +
        '\nCONTRACT_OUTPUT = $@\nPROGRAM_LDFLAGS += -Map=harmless.map\n'
        'userland/cmds/cfg.o: extra_header.h\n'
        'all: innocent-echo\ninnocent-echo:\n\t@echo $(CC) $(PROGRAM_FLAGS) -c harmless.c\n')
    try:
        check_recipes(tree, (PRODUCT_GOALS[0],))
        print('GREEN unrelated output variable, LDFLAGS, header prerequisite and echo')
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


def check_compiler_wrappers():
    command = 'i386-elf-gcc -std=gnu89 -c lib/sqlite3/sqlite3.c -o sqlite.o'
    for prefix in ('ccache ', '/usr/bin/ccache compiler_check=content base_dir=/repo '):
        assert list(compiler_commands(prefix + command)) == list(compiler_commands(command))
    assert list(compiler_commands('echo /usr/bin/ccache ' + command)) == []


def main():
    check_compiler_wrappers()
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--ppc', action='store_true', help='manual/PM only; never used by CHECK')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='type-contract-') as temporary:
        directory = Path(temporary)
        tree = directory / 'recipes'
        recipe_tree(tree)
        if not args.ppc:
            check_recipes(tree)
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
            raise SystemExit('required PPC toolchain unavailable (manual/PM probe)')
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
            for header in ('os32_kapi_shared.h', 'os32_gui_shared.h', 'os32_gfx2.h'):
                result = syntax(cc, ['-m32', '-std=gnu89', '-Werror', '-Isdk/include/os32'],
                                directory, '#include "' + header + '"\n')
                assert result.returncode == 0, result.stderr
            runtime(directory)
            probe_artifacts(directory, profile, args.mutate)
            direct_objects(directory)
            print('PASS host32 runtime: ILP32, signed char8, enum4, u64=8/4, LE')
        if args.mutate:
            if not args.ppc:
                recipe_mutations(tree)
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
