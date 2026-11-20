#!/usr/bin/env python3
"""Source/include ownership, legacy order and feature closure from real make."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_profile import query
from build_fixture import copy_build_tree
ROOT = Path(__file__).resolve().parents[2]
FEATURES = 'V86 X87 PC98_DEVICES GEFORCE TRIDENT CIRRUS PCM FM LAN KCG CD FEP SQLITE HWDEV KDEBUG'.split()


def check_sqlite(root):
    c_units = {'kernel/sqlite_engine.c', 'kernel/sqlite_outer.c'}
    x86_stack = 'lib/sqlite3/sqlite_stack.asm'
    ppc_stack = 'arch/ppc/sqlite_stack_ppc.S'
    ppc = ['ARCH=ppc', 'PLATFORM=powermac']
    for arch, options in [('x86', ['FEATURE_' + f + '=0' for f in FEATURES if f != 'SQLITE']),
                          ('ppc', [*ppc, 'IMAGE=kboot-link-sh'])]:
        for enabled in (0, 1):
            profile = query(root, [*options, f'FEATURE_SQLITE={enabled}'], config_only=True)
            for unit in c_units:
                assert profile['C_KERNEL'].count(unit) == enabled
            smoke = 'lib/sqlite3/os32_sqlite_test.c'
            assert profile['C_KERNEL'].count(smoke) == (enabled and arch == 'x86')
            assert profile['ASM_KERNEL'].count(x86_stack) == (enabled and arch == 'x86')
            assert profile['GAS_KERNEL'].count(ppc_stack) == (enabled and arch == 'ppc')
            assert x86_stack not in profile['GAS_KERNEL'] and ppc_stack not in profile['ASM_KERNEL']
    try:
        query(root, [*ppc, 'IMAGE=kboot-link', 'FEATURE_SQLITE=1'], config_only=True)
    except subprocess.CalledProcessError as error:
        assert 'FEATURE_SQLITE requires FEATURE_EXEC' in error.stderr
    else:
        raise AssertionError('SQLite selected without filesystem services')


def check_loop(root):
    for image in ('pc98', 'kboot-core', 'kboot-link', 'kboot-shell', 'kboot-sh', 'kboot-link-sh'):
        options = [] if image == 'pc98' else ['ARCH=ppc', 'PLATFORM=powermac']
        units = query(root, [*options, 'IMAGE=' + image], config_only=True)['C_KERNEL']
        enabled = image in ('pc98', 'kboot-shell', 'kboot-sh', 'kboot-link-sh')
        for unit in ('kernel/dev/device_loop.c', 'drivers/block/loop_dev.c'):
            assert units.count(unit) == enabled, (image, unit)


def check_fep(root):
    x86 = ['FEATURE_' + f + '=0' for f in FEATURES if f not in ('SQLITE', 'FEP')]
    ppc = ['ARCH=ppc', 'PLATFORM=powermac', 'IMAGE=kboot-link-sh']
    for options in (x86, ppc):
        profiles = [query(root, [*options, 'FEATURE_SQLITE=1', f'FEATURE_FEP={enabled}'],
                          config_only=True) for enabled in (0, 1)]
        unit = 'kernel/ime/fep_module.c'
        off, on = profiles
        assert unit not in off['C_KERNEL'] and on['C_KERNEL'].count(unit) == 1
        assert [name for name in on['C_KERNEL'] if name != unit] == off['C_KERNEL']
        for key in ('ASM_KERNEL', 'GAS_KERNEL'):
            assert on[key] == off[key]
        try:
            query(root, [*options, 'FEATURE_SQLITE=0', 'FEATURE_FEP=1'], config_only=True)
        except subprocess.CalledProcessError as error:
            assert 'FEATURE_FEP requires FEATURE_SQLITE' in error.stderr
        else:
            raise AssertionError('FEP selected without SQLite')


def check(root):
    expected = json.loads((ROOT / 'tools/tests/fixtures/build_profile_x86.json').read_text())
    actual = query(root)
    # The minimal kernel query omits the independently linked GPU module.
    actual['GEFORCE_DISPLAY_STATE_SOURCES'] = query(
        root, ['-f', 'build/geforce-module.mk'])['GEFORCE_DISPLAY_STATE_SOURCES']
    for name, value in expected.items():
        assert actual[name] == value, name
    assert actual['C_KERNEL'].count('kernel/sqlite_engine.c') == 1
    assert actual['ASM_KERNEL'].count('lib/sqlite3/sqlite_stack.asm') == 1
    assert not any('platform/powermac/hostfs' in name for name in actual['C_KERNEL'])
    c = query(root, ['KSTRING_C_SRC=lib/kstring_c.c'])['C_KERNEL']
    assert c.count('lib/kstring_c.c') == 1
    assert c.index('lib/kstring_c.c') == c.index('lib/divdi3.c') + 1
    # Top-level ARCH=ppc selects the product default image (kboot-link-sh) from the same owner sets.
    ppc = query(root, ['ARCH=ppc', 'PLATFORM=powermac'], config_only=True)
    link = json.loads((ROOT / 'tools/tests/fixtures/build_profile_ppc.json').read_text())['link-sh']
    assert ppc['C_KERNEL'] == link['KB_C'] and ppc['GAS_KERNEL'] == link['KB_ASM'] and not ppc['ASM_KERNEL']
    for enabled in (0, 1):
        profile = query(root, ['ARCH=ppc', 'PLATFORM=powermac', f'FEATURE_KCG={enabled}'], config_only=True)
        assert profile['C_KERNEL'].count('drivers/video/kcg/kcg.c') == enabled
        assert profile['C_KERNEL'].count('kernel/dev/device_kcg.c') == enabled
        assert 'drivers/video/kcg/kcg_module.c' not in profile['C_KERNEL']
    assert 'exec/ring3_ls.c' in actual['C_KERNEL']
    assert actual['C_KERNEL'].index('exec/ring3_ls.c') == expected['C_KERNEL'].index('exec/ring3_ls.c')
    off = query(root, ['FEATURE_' + name + '=0' for name in FEATURES], config_only=True)
    assert not any(path in name for name in off['C_KERNEL'] for path in
                   ('kernel/v86/', 'platform/pc98/legacy/'))
    assert off['C_KERNEL'].count('kernel/dev/device_kcg.c') == 1
    assert 'drivers/video/kcg/kcg.c' not in off['C_KERNEL']
    assert not any('geforce/' in name for name in off['C_KERNEL'])
    for assignments in (['FEATURE_UNKNOWN=1'], ['ARCH=ppc','PLATFORM=powermac','FEATURE_V86=1'], ['FEATURE_KDEBUG=0'],
                        ['FEATURE_LINK=0'], ['IMAGE=kboot-link'], ['IMAGE=unknown'],
                        ['ARCH=ppc', 'PLATFORM=powermac', 'IMAGE=pc98']):
        try:
            query(root, assignments, config_only=True)
        except subprocess.CalledProcessError:
            pass
        else:
            raise AssertionError(assignments)


KBOOT_QUERY = ('.PHONY: kboot-query\nkboot-query:\n'
               '\t@echo "KB_ASM=$(KB_ASM)"\n\t@echo "KB_C=$(KB_C)"\n'
               '\t@echo "KB_OUT=$(KB_OUT)"\n\t@echo "KB_VFS=$(KB_VFS)"\n'
               '\t@echo "KB_SQLITE_INC=$(KB_SQLITE_INC)"\n'
               '\t@echo "KB_SQLITE_AUDIT=$(KB_SQLITE_AUDIT)"\n'
               '\t@echo "KB_PENDING=$(KB_PENDING_OBJ)"\n'
               '\t@echo "KB_MODULES=$(MODULE_FEATURES)"\n')


def copy_kboot_names(root):
    # Wildcards in the kboot owner sets and link order need only the file names.
    for pattern in ('exec/*.c', 'arch/ppc/exec_*.c', 'drivers/usb/*.c'):
        for source in ROOT.glob(pattern):
            target = root / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.touch()


def check_kboot(root):
    """Every PowerPC image selects exactly the pinned sources, in owner-set order (no link-order list)."""
    expected = json.loads((ROOT / 'tools/tests/fixtures/build_profile_ppc.json').read_text())
    (root / 'kboot-query.mk').write_text(KBOOT_QUERY)
    command = ['make', '-s', '-f', 'build/config.mk', '-f', 'build/targets/ppc-powermac.mk', '-f', 'kboot-query.mk',
               'ARCH=ppc', 'PLATFORM=powermac']
    for name, image in expected.items():
        # 像は旗でも名前でも選べる (build/images/ppc-flags.mk)。NEGATIVE の像は旗だけ。
        selectors = [image['flags']] + ([] if 'NEGATIVE=1' in image['flags'] else [['IMAGE=kboot-' + name]])
        for selector in selectors:
            out = subprocess.check_output([*command, *selector, 'kboot-query'], cwd=root, text=True,
                                          stderr=subprocess.PIPE)
            actual = dict(line.split('=', 1) for line in out.splitlines() if line.startswith('KB_'))
            assert actual['KB_OUT'] == image['KB_OUT'], (name, selector)
            ext2 = 'fs/ext2/ext2_'
            formatter = {ext2 + stem for stem in ('fmt', 'layout', 'time')}
            pending = set(actual['KB_PENDING'].split())
            for unit in formatter:
                assert actual['KB_OUT'] + '/' + unit + '.o' in pending, (name, unit)
            ide = ext2 + 'medium_ide'
            assert actual['KB_OUT'] + '/' + ide + '.o' not in pending, name
            assert {unit + '.c' for unit in formatter | {ide}}.isdisjoint(actual['KB_C'].split()), name
            enabled = name in ('shell', 'sh', 'link-sh')
            assert 'sqlite' not in actual['KB_MODULES'].split()
            for key in ('KB_VFS', 'KB_SQLITE_INC', 'KB_SQLITE_AUDIT'):
                assert bool(actual[key]) == enabled
            if enabled:
                disabled = subprocess.check_output([*command, *selector, 'FEATURE_SQLITE=0',
                                                    'kboot-query'], cwd=root, text=True)
                assert 'arch/ppc/sqlite_stack_ppc.S' not in disabled
                for key in ('KB_VFS', 'KB_SQLITE_INC', 'KB_SQLITE_AUDIT'):
                    assert key + '=\n' in disabled
            for key in ('KB_ASM', 'KB_C'):
                assert actual[key].split() == image[key], (name, key, selector)
    assert subprocess.run([*command, 'KBOOT_LINK=0', 'KBOOT_SH=0', 'KBOOT_SHELL=1',
                           'NEGATIVE=1', 'kboot-query'], cwd=root, capture_output=True).returncode != 0
    # 名前と旗の両方を与えて食い違えば止まる。合っていれば通る。
    assert subprocess.run([*command, 'IMAGE=kboot-core', 'KBOOT_SH=1', 'kboot-query'], cwd=root,
                          capture_output=True).returncode != 0
    assert subprocess.run([*command, 'IMAGE=kboot-sh', 'KBOOT_SH=1', 'KBOOT_LINK=0', 'kboot-query'], cwd=root,
                          capture_output=True).returncode == 0


def check_user_selection(root):
    (root / 'user-query.mk').write_text(
        'include build/config.mk\n.PHONY: user-query\n'
        'user-query:; @echo "USER=$(SDK_TARGET_CRT_USER)|$(SDK_TARGET_APP_LINK)|$(SDK_USER_LIBC)"\n')
    def selected(*args):
        return subprocess.check_output(['make', '-s', '-f', 'user-query.mk', 'user-query', *args],
                                       cwd=root, text=True).strip()
    assert selected('ARCH=ppc', 'PLATFORM=powermac') == (
        'USER=sdk/crt/ppc/crt0.o sdk/crt/ppc/crt0_c.o|sdk/link/ppc/app.ld|')
    assert selected('NANO_LIB=nano.a') == (
        'USER=sdk/crt/crt0.o sdk/crt/crt0_c.o sdk/crt/syscalls.o sdk/crt/help.o|sdk/link/app.ld|nano.a')


def check_archives(root):
    # Query the complete product graph without traversing or building products.
    (root / 'archive-query.mk').write_text('$(info QUERY_ARCHIVES=$(ALL_LIB_ARCHIVES))\n.PHONY: archive-query\narchive-query:; @:\n')
    result = subprocess.run(['make', '-pn', '-f', 'Makefile', '-f', 'archive-query.mk',
                             'archive-query'], cwd=root, check=True,
                            capture_output=True, text=True)
    expected = next(line.split('=', 1)[1].split() for line in result.stdout.splitlines()
                    if line.startswith('QUERY_ARCHIVES='))
    archives = {}
    for line in result.stdout.splitlines():
        if ': ' not in line or line.startswith(('#', '\t')):
            continue
        target, prerequisites = line.split(': ', 1)
        if target in expected:
            normal = prerequisites.split('|')[0].split()
            if normal:
                archives[target] = normal
    assert len(archives) >= 16 and set(archives) == set(expected), archives
    for target, prerequisites in archives.items():
        assert all(name.endswith(('.o', '.a')) for name in prerequisites), (target, prerequisites)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    # Copy only make inputs; compiler and generated products are unnecessary.
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        copy_build_tree(root)
        copy_kboot_names(root)
        (root / 'Makefile').write_text('include build/config.mk\n-include build/kernel-objects.mk\ninclude build/kernel.mk\n')
        # No libgcc invocation is allowed by these query-only tests.
        check(root)
        check_sqlite(root)
        check_fep(root)
        check_loop(root)
        check_user_selection(root)
        check_kboot(root)
        if args.mutate:
            for path, anchor, mutant, expected, label in (
                    ('build/sources/common.mk', 'kernel/sqlite_engine.c $(C_EXT2_FORMATTER)',
                     'kernel/sqlite_engine.c', AssertionError, 'formatter omitted from PowerPC compile'),
                    ('build/sources/common.mk', 'C_COMMON_CORE = ', 'C_COMMON_CORE = $(C_EXT2_FORMATTER) ',
                     AssertionError, 'formatter linked into PowerMac'),
                    ('build/images/kboot-link-sh.mk', 'FEATURE_LOOP = 1', 'FEATURE_LOOP = 0',
                     AssertionError, 'loop disabled on kboot-sh'),
                    ('build/images/kboot-link.mk', 'FEATURE_LOOP = 0', 'FEATURE_LOOP = 1',
                     subprocess.CalledProcessError, 'loop enabled on kboot-link'),
                    ('build/select.mk', '$(error FEATURE_FEP requires FEATURE_SQLITE)', '',
                     AssertionError, 'missing FEP prerequisite'),
                    ('build/features/fep.mk', 'C_FEATURES += kernel/ime/fep_module.c', '',
                     AssertionError, 'missing FEP kernel loader'),
                    ('build/select.mk', '$(error FEATURE_SQLITE requires FEATURE_EXEC)', '',
                     AssertionError, 'missing SQLite prerequisite'),
                    ('build/features/sqlite.mk', 'ifeq ($(ARCH),ppc)', 'ifeq ($(ARCH),other)',
                     AssertionError, 'x86 SQLite stack on PowerPC'),
                    ('build/sources/hooks.mk', 'HOOK_gfx = $(FEATURE_EXEC)', 'HOOK_gfx = $(FEATURE_PC98_PENDING)',
                     AssertionError, 'PowerMac exec uses absent graphics hooks'),
                    ('build/platform/powermac.mk', 'platform/powermac/rh_observe.c platform/powermac/ofw.c',
                     'platform/powermac/ofw.c platform/powermac/rh_observe.c',
                     AssertionError, 'reordered kboot sources'),
                    ('build/platform/powermac.mk', ' platform/powermac/nic_gem.c', '',
                     AssertionError, 'dropped kboot owner entry'),
                    ('build/sources/common.mk', 'C_COMMON_LOOP = kernel/dev/device_loop.c',
                     'C_COMMON_LOOP = kernel/dev/device_loop.c kernel/dev/unordered.c',
                     AssertionError, 'kboot source added to an owner set'),
                    ('build/images/kboot-link.mk', 'FEATURE_LINK = 1', 'FEATURE_LINK = 0',
                     AssertionError, 'kboot image feature value'),
                    ('build/images/ppc-flags.mk', 'KBOOT_SH := $(if $(filter kboot-sh kboot-link-sh,$(IMAGE)),1,0)',
                     'KBOOT_SH := 0', subprocess.CalledProcessError, 'image name no longer sets its flags'),
                    ('build/images/ppc-flags.mk', 'ifneq ($(call ppc_image,$(IMAGE)),$(KB_IMAGE))', 'ifneq ($(IMAGE),$(IMAGE))',
                     AssertionError, 'image name and flags may disagree')):
                target = root / path
                source = target.read_text()
                assert source.count(anchor) == 1, (path, anchor)
                target.write_text(source.replace(anchor, mutant))
                try:
                    if label.startswith('loop '):
                        check_loop(root)
                    elif 'FEP' in label:
                        check_fep(root)
                    elif path in ('build/select.mk', 'build/features/sqlite.mk'):
                        check_sqlite(root)
                    else:
                        check_kboot(root)
                except expected as error:
                    if label == 'loop enabled on kboot-link':
                        assert 'FEATURE_LOOP requires FEATURE_EXEC' in error.stderr, error.stderr
                    print('RED: ' + label + ' rejected at runtime')
                else:
                    raise SystemExit(label + ' mutation survived')
                target.write_text(source)
            check_kboot(root)
            target = root / 'build/sdk-target.mk'
            source = target.read_text()
            anchor = '--make-user --root sdk'
            assert source.count(anchor) == 1
            target.write_text(source.replace(anchor, '--make --root sdk'))
            try:
                check_user_selection(root)
            except AssertionError:
                print('RED: USER capability selection rejected at runtime')
            else:
                raise SystemExit('USER selection mutation survived')
            target.write_text(source)
            order = root / 'build/sources/order.mk'
            source = order.read_text()
            anchor = 'kdebug/gf5_test.c kdebug/gf_display_test.c'
            assert source.count(anchor) == 1
            order.write_text(source.replace(anchor, 'kdebug/gf_display_test.c kdebug/gf5_test.c'))
            try:
                check(root)
            except AssertionError:
                print('RED: reordered legacy sources rejected at runtime')
            else:
                raise SystemExit('mutation survived')
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        # Preserve wildcard source discovery and all real make includes.
        names = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT).decode().split('\0')
        for name in names:
            if name == 'Makefile' or name.startswith(('build/', 'userland/', 'lib/', 'sdk/', 'tools/')):
                source = ROOT / name
                if source.is_file():
                    target = root / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, target)
        check_archives(root)
        if args.mutate:
            target = root / 'build/targets/x86-pc98.mk'
            source = target.read_text()
            anchor = '$(filter %.o,$(ALL_TARGET_OBJECTS))'
            assert source.count(anchor) == 1
            target.write_text(source.replace(anchor, '$(filter %.o %.a,$(ALL_TARGET_OBJECTS))'))
            try:
                check_archives(root)
            except AssertionError:
                print('RED: archive profile prerequisites rejected at runtime')
            else:
                raise SystemExit('archive mutation survived')
            target.write_text(source)
            check_archives(root)
    print('PASS build selection')
