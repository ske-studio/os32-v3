#!/usr/bin/env python3
"""S1: one SDK target profile feeds make; unsupported targets are refused early."""
CHECK = dict(name='sep5-sdk-host', tier=1, needs=[], budget=20,
    covers=['tools/tests/build_fixture.py', 'sdk/target_profile.py', 'sdk/target_profile_cli.py', 'build/sdk-target.mk',
            'build/config.mk', 'build/**/*.mk', 'Makefile', 'sdk/example/hello/Makefile',
            'userland/rust/.cargo/config.toml', 'userland/gshell/.cargo/config.toml',
            'sdk/kapi_abi_x86.py', 'sdk/link/*.ld',
            'sdk/rust/i686-os32-none.json', 'sdk/crt/crt0.asm', 'sdk/crt/crt0_c.c',
            'sdk/crt/syscalls.c', 'sdk/crt/help.c', 'userland/lib/rt/ls.c',
            'tools/tests/test_sep5_sdk.py'])
import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from build_fixture import copy_build_tree

ROOT = Path(__file__).resolve().parents[2]
PROFILE, CLI = 'sdk/target_profile.py', 'sdk/target_profile_cli.py'
MK, CONFIG = 'build/sdk-target.mk', 'build/config.mk'
CRT_USER = ('crt/crt0.o', 'crt/crt0_c.o', 'crt/syscalls.o', 'crt/help.o')
CRT_RESIDENT = ('crt/crt0.o', 'crt/crt0_c.o', 'crt/resident/syscalls.o', 'crt/help.o',
                'crt/resident/nano_adapter.o')
# Written out here on purpose: the profile must match what the tree already uses.
EXPECTED = dict(call_abi='i386-cdecl', elf_emulation='elf_i386', crt_user=CRT_USER,
                crt_resident=CRT_RESIDENT, app_link='link/app.ld',
                app_sys_link='link/app_sys.ld', shlib_link='link/shlib.ld',
                rust='rust/i686-os32-none.json')
TOKENS = ('SDK_TARGET_CALL_ABI=i386-cdecl SDK_TARGET_ELF_EMULATION=elf_i386 '
          'SDK_TARGET_CRT_USER=sdk/crt/crt0.o,sdk/crt/crt0_c.o,sdk/crt/syscalls.o,sdk/crt/help.o '
          'SDK_TARGET_CRT_RESIDENT=sdk/crt/crt0.o,sdk/crt/crt0_c.o,sdk/crt/resident/syscalls.o,'
          'sdk/crt/help.o,sdk/crt/resident/nano_adapter.o SDK_TARGET_APP_LINK=sdk/link/app.ld '
          'SDK_TARGET_APP_SYS_LINK=sdk/link/app_sys.ld SDK_TARGET_SHLIB_LINK=sdk/link/shlib.ld '
          'SDK_TARGET_RUST=sdk/rust/i686-os32-none.json\n')
NAMES = ('SDK_TARGET_CALL_ABI SDK_TARGET_ELF_EMULATION SDK_TARGET_CRT_USER '
         'SDK_TARGET_CRT_RESIDENT SDK_TARGET_APP_LINK SDK_TARGET_APP_SYS_LINK '
         'SDK_TARGET_SHLIB_LINK SDK_TARGET_RUST').split()
EVIL = ['SDK_TARGET_SUPPORTED=1', 'SDK_TARGET_STATUS=0', 'SDK_TARGET_CRT_USER=evil.o',
        'SDK_TARGET_APP_LINK=evil.ld', 'SDK_TARGET_TOKENS=SDK_TARGET_RUST=evil.json']
# The list and helpers of sdk-target.mk themselves must not be replaceable either.
EVIL_LISTS = (['SDK_TARGET_NAMES=SDK_TARGET_SUPPORTED'], ['SDK_TARGET_NAMES='],
              ['sdk_target_comma=;'], ['sdk_target_name=SDK_TARGET_SUPPORTED'],
              ['sdk_target_value=evil.o'])
# (name, file, unique anchor, replacement, label of the first check that must fail)
MUTATIONS = (
    ('ppc-sdk-enabled', PROFILE, "sdk=False, **dict.fromkeys(SDK_FIELDS)", "sdk=True, **dict.fromkeys(SDK_FIELDS)", 'ppc-refused'),
    ('crt-order', PROFILE, "crt_user=('crt/crt0.o', 'crt/crt0_c.o',", "crt_user=('crt/crt0_c.o', 'crt/crt0.o',", 'x86-profile'),
    ('app-link-sys', PROFILE, "app_link='link/app.ld'", "app_link='link/app_sys.ld'", 'x86-profile'),
    ('rust-target', PROFILE, "rust='rust/i686-os32-none.json'", "rust='rust/i686-unknown-none.json'", 'x86-profile'),
    ('incomplete-accepted', PROFILE, '    if not all(values.values()):\n', '    if False:\n', 'incomplete'),
    ('cli-refusal-status', CLI, '        return REFUSED\n', '        return 0\n', 'ppc-refused'),
    ('cli-root-dropped', CLI, '            items = [prefix + item for item in items]\n', '            pass\n', 'cli-tokens'),
    ('make-crt-resident', CONFIG, 'CRT0_USER_OBJ = $(SDK_TARGET_CRT_USER) $(NANO_LIB)', 'CRT0_USER_OBJ = $(SDK_TARGET_CRT_RESIDENT) $(NANO_LIB)', 'x86-make'),
    ('make-link-sys', CONFIG, '-T $(SDK_TARGET_APP_LINK) -nostdlib', '-T $(SDK_TARGET_APP_SYS_LINK) -nostdlib', 'x86-make'),
    ('clear-overridable', MK, '$(eval override $(name) :=)', '$(eval $(name) :=)', 'ppc-make-empty'),
    ('refusal-as-support', MK, 'override SDK_TARGET_SUPPORTED := $(if $(filter 0,$(SDK_TARGET_STATUS)),1,0)',
     'override SDK_TARGET_SUPPORTED := $(if $(filter 0 3,$(SDK_TARGET_STATUS)),1,0)', 'ppc-make-empty'),
    ('value-overridable', MK, '    $(eval override $(call sdk_target_name,$(token)) := ',
     '    $(eval $(call sdk_target_name,$(token)) := ', 'x86-make'),
    ('names-overridable', MK, 'override SDK_TARGET_NAMES := SDK_TARGET_CALL_ABI', 'SDK_TARGET_NAMES := SDK_TARGET_CALL_ABI', 'ppc-make-empty'),
    ('helper-overridable', MK, 'override sdk_target_value = ', 'sdk_target_value = ', 'x86-override'),
    ('crt-eager', CONFIG, 'CRT0_USER_OBJ = $(SDK_TARGET_CRT_USER) $(NANO_LIB)', 'CRT0_USER_OBJ := $(SDK_TARGET_CRT_USER) $(NANO_LIB)', 'x86-make'),
    ('defect-as-refusal', CLI, '    except target_profile.Unsupported as error:', '    except ValueError as error:', 'ppc-refused'),
    ('elf-unchecked', MK, 'ifneq ($(SDK_TARGET_ELF_EMULATION),$(ELF_EMULATION))', 'ifneq (,)', 'elf-disagree'),
    ('broken-query-tolerated', MK, 'ifeq ($(filter 0 3,$(SDK_TARGET_STATUS)),)', 'ifeq (x,)', 'defect-status'),
)


class Failed(Exception):
    pass


def need(ok, label, detail=''):
    if not ok:
        raise Failed(label, detail)


def clean_env(root):
    env = dict(os.environ, CROSS_DIR=str(root / 'cross'))
    for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'GNUMAKEFLAGS'):
        env.pop(name, None)
    return env


def expand(root, variables, *args, target='x86-pc98', via='argv', after=''):
    """Expand make variables through build/config.mk alone (no product graph).
    args reach make on the command line, through MAKEFLAGS, or as environment + -e."""
    arch, platform = target.split('-')
    recipe = ''.join('\t@$(info %s=$(%s))\n' % (name, name) for name in variables)
    env, flags = clean_env(root), []
    if via == 'MAKEFLAGS':
        env['MAKEFLAGS'] = ' '.join(args)
        args = ()
    elif via == 'environment':
        env.update(arg.split('=', 1) for arg in args)
        args, flags = (), ['-e']
    result = subprocess.run(
        ['make', '--no-print-directory', '-rRsf', '-', *flags, 'ARCH=' + arch,
         'PLATFORM=' + platform, 'BUILD_OUT=output', *args, '__query'],
        input='include build/config.mk\n' + after + '.PHONY: __query\n__query:\n' + recipe + '\t@:\n',
        cwd=root, env=env, text=True, capture_output=True, timeout=60)
    values = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
    return result, values


def cli(root, *args):
    return subprocess.run([sys.executable, '-B', str(root / CLI), *args], cwd=root,
                          text=True, capture_output=True, timeout=30)


def load(root):
    spec = importlib.util.spec_from_file_location('fixture_target_profile', root / PROFILE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def refuse(call, text):
    try:
        call()
    except ValueError as error:
        return str(error) == text
    return False


def entry_rejected(root, target):
    """A product entry on the unsupported target: no tool call, no output."""
    before = {p.relative_to(root) for p in root.rglob('*') if p.is_file()}
    result = subprocess.run(['make', '-j1', '--no-print-directory', 'ARCH=ppc', 'PLATFORM=powermac',
        'BUILD_OUT=output', 'CC=./spy', 'AS=./spy', 'LD=./spy', 'OBJCOPY=./spy', 'AR=./spy',
        *EVIL, target], cwd=root, env=clean_env(root), capture_output=True, text=True, timeout=60)
    after = {p.relative_to(root) for p in root.rglob('*') if p.is_file()}
    return (not (root / 'calls').exists() and after == before and result.returncode != 0
            and 'unsupported' in result.stderr)


def checks(root):
    """Run every expectation; raise Failed(label) at the first one that does not hold."""
    profile = load(root)
    need(profile.sdk_values('x86-pc98') == EXPECTED, 'x86-profile')
    need(profile.require('x86-pc98', 'sdk')['sdk'] is True, 'x86-profile')
    tokens = cli(root, '--target', 'x86-pc98', '--make', '--root', 'sdk')
    need((tokens.returncode, tokens.stdout, tokens.stderr) == (0, TOKENS, ''), 'cli-tokens', tokens)
    need(cli(root, '--target', 'x86-pc98', '--require', 'sdk').returncode == 0, 'cli-tokens')

    # NANO_LIB is defined after the config (build/programs.mk): the CRT list must stay lazy.
    result, values = expand(root, ['CRT0_USER_OBJ', 'CRT0_RESIDENT_OBJ', 'CRT0_OBJ', 'NANO_LIB',
                                   'PROGRAM_LDFLAGS', 'SDK_TARGET_SUPPORTED', *NAMES],
                            after='NANO_LIB = output/lib/libos32nano.a\n')
    need(result.returncode == 0, 'x86-make', result.stderr)
    made = {name: value.replace(',', ' ') for name, value in
            (token.split('=') for token in TOKENS.split())}
    user = made['SDK_TARGET_CRT_USER']
    need(values['CRT0_USER_OBJ'].split() == (user + ' ' + values['NANO_LIB']).split() and
         values['CRT0_OBJ'] == values['CRT0_USER_OBJ'] and
         values['CRT0_USER_OBJ'] == user + ' output/lib/libos32nano.a' and
         values['CRT0_RESIDENT_OBJ'] == made['SDK_TARGET_CRT_RESIDENT'] and
         values['PROGRAM_LDFLAGS'].startswith('-m elf_i386 -T sdk/link/app.ld -nostdlib --nmagic ') and
         values['SDK_TARGET_SUPPORTED'] == '1' and
         values['SDK_TARGET_APP_SYS_LINK'] == 'sdk/link/app_sys.ld' and
         values['SDK_TARGET_SHLIB_LINK'] == 'sdk/link/shlib.ld' and
         values['SDK_TARGET_RUST'] == 'sdk/rust/i686-os32-none.json' and
         values['SDK_TARGET_CALL_ABI'] == 'i386-cdecl', 'x86-make', values)

    for target, message in (('ppc-powermac', 'unsupported sdk target ppc-powermac'),
                            ('m68k-x68000', 'unknown target m68k-x68000')):
        need(refuse(lambda: profile.sdk_values(target), message), 'ppc-refused', target)
        for mode in (['--make', '--root', 'sdk'], ['--require', 'sdk']):
            refused = cli(root, '--target', target, *mode)
            need((refused.returncode, refused.stdout, refused.stderr) == (3, '', message + '\n'),
                 'ppc-refused', refused)
    unknown = cli(root, '--target', 'x86-pc98', '--require', 'telepathy')
    need((unknown.returncode, unknown.stdout) == (2, ''), 'ppc-refused', unknown)

    partial = dict(profile.PROFILES['x86-pc98'], rust=None)
    profile.PROFILES['partial'] = partial
    need(refuse(lambda: profile.sdk_values('partial'), 'incomplete sdk target partial'), 'incomplete')

    # The unsupported target carries no SDK value, whatever the command line says.
    result, values = expand(root, ['SDK_TARGET_SUPPORTED', 'CRT0_RESIDENT_OBJ', *NAMES], *EVIL,
                            target='ppc-powermac')
    need(result.returncode == 0 and values.get('SDK_TARGET_SUPPORTED') == '0' and
         values.get('CRT0_RESIDENT_OBJ') == '' and all(values.get(name) == '' for name in NAMES),
         'ppc-make-empty', (result.stderr, values))
    for extra in EVIL_LISTS:
        result, values = expand(root, ['SDK_TARGET_SUPPORTED', *NAMES], *EVIL, *extra,
                                target='ppc-powermac')
        need(result.returncode == 0 and values.get('SDK_TARGET_SUPPORTED') == '0' and
             all(values.get(name) == '' for name in NAMES), 'ppc-make-empty', (extra, values))
    for target in ('sdk', 'programs', 'libs', 'boot'):
        need(entry_rejected(root, target), 'ppc-entries', target)

    result, values = expand(root, ['SDK_TARGET_SUPPORTED', 'CRT0_USER_OBJ', 'NANO_LIB', *NAMES], *EVIL)
    need(result.returncode == 0 and values['SDK_TARGET_SUPPORTED'] == '1' and
         values['CRT0_USER_OBJ'].split() == (user + ' ' + values['NANO_LIB']).split() and
         values['SDK_TARGET_APP_LINK'] == 'sdk/link/app.ld' and
         values['SDK_TARGET_RUST'] == 'sdk/rust/i686-os32-none.json', 'x86-override', values)

    for extra in EVIL_LISTS:
        for via in ('argv', 'MAKEFLAGS', 'environment'):
            result, values = expand(root, ['SDK_TARGET_SUPPORTED', *NAMES], *EVIL, *extra, via=via)
            need(result.returncode == 0 and values.get('SDK_TARGET_SUPPORTED') == '1' and
                 values.get('SDK_TARGET_CRT_USER') == user and
                 values.get('SDK_TARGET_APP_LINK') == 'sdk/link/app.ld' and
                 values.get('SDK_TARGET_RUST') == 'sdk/rust/i686-os32-none.json',
                 'x86-override', (extra, via, result.stderr, values))

    result, _ = expand(root, ['CRT0_USER_OBJ'], 'ELF_EMULATION=elf32ppc')
    need(result.returncode != 0 and 'disagree on the ELF emulation' in result.stderr,
         'elf-disagree', result.stderr)

    # A defect is not a refusal: status 2, and make stops instead of linking without a CRT.
    bad_root = cli(root, '--target', 'x86-pc98', '--make', '--root', 'bad root')
    need((bad_root.returncode, bad_root.stdout) == (2, ''), 'defect-status', bad_root)
    intact = (root / PROFILE).read_text()
    try:
        hole = "shlib_link='link/shlib.ld', rust='rust/i686-os32-none.json')"
        assert intact.count(hole) == 1
        (root / PROFILE).write_text(intact.replace(hole, "shlib_link='link/shlib.ld', rust=None)"))
        partial = cli(root, '--target', 'x86-pc98', '--make', '--root', 'sdk')
        need((partial.returncode, partial.stdout, partial.stderr) ==
             (2, '', 'incomplete sdk target x86-pc98\n'), 'defect-status', partial)
        result, _ = expand(root, ['CRT0_USER_OBJ'])
        need(result.returncode != 0 and 'sdk target profile query failed' in result.stderr,
             'defect-status', result.stderr)
    finally:
        (root / PROFILE).write_text(intact)

    original = (root / CLI).read_text()
    try:
        for broken in ('raise SystemExit(1)\n', 'import no_such_module\n', None):
            if broken is None:
                (root / CLI).unlink()
            else:
                (root / CLI).write_text(broken)
            result, _ = expand(root, ['CRT0_USER_OBJ'])
            need(result.returncode != 0 and 'sdk target profile query failed' in result.stderr,
                 'query-broken', (broken, result.stderr))
    finally:
        (root / CLI).write_text(original)


def tree_agrees():
    """The other spellings of the same set in the tree (S2 connects them)."""
    hello = (ROOT / 'sdk/example/hello/Makefile').read_text()
    assert 'LDFLAGS = -m elf_i386 -T $(OS32_SDK)/link/app.ld -nostdlib' in hello
    assert ('CRT  = $(OS32_SDK)/crt/crt0.o $(OS32_SDK)/crt/crt0_c.o \\\n'
            '       $(OS32_SDK)/crt/syscalls.o $(OS32_SDK)/crt/help.o\n') in hello
    for config in ('userland/rust/.cargo/config.toml', 'userland/gshell/.cargo/config.toml'):
        assert 'target = "../../sdk/' + EXPECTED['rust'] + '"' in (ROOT / config).read_text(), config
    # 字面で名指しする (検査の入力の走査が追えるように): profile の値が指す実物。
    assert (EXPECTED['app_link'], EXPECTED['app_sys_link'], EXPECTED['shlib_link'], EXPECTED['rust']) == (
        'link/app.ld', 'link/app_sys.ld', 'link/shlib.ld', 'rust/i686-os32-none.json')
    assert (ROOT / 'sdk/link/app.ld').is_file() and (ROOT / 'sdk/link/app_sys.ld').is_file()
    assert (ROOT / 'sdk/link/shlib.ld').is_file() and (ROOT / 'sdk/rust/i686-os32-none.json').is_file()
    assert CRT_USER == ('crt/crt0.o', 'crt/crt0_c.o', 'crt/syscalls.o', 'crt/help.o')
    assert (ROOT / 'sdk/crt/crt0.asm').is_file() and (ROOT / 'sdk/crt/crt0_c.c').is_file()
    assert (ROOT / 'sdk/crt/syscalls.c').is_file() and (ROOT / 'sdk/crt/help.c').is_file()
    assert 'cdecl' in (ROOT / 'sdk/kapi_abi_x86.py').read_text().splitlines()[0]


def fixture(root, changes=()):
    copy_build_tree(root)
    shutil.copy2(ROOT / 'Makefile', root / 'Makefile')
    for path, old, new in changes:
        text = (root / path).read_text()
        assert text.count(old) == 1, (path, old, text.count(old))
        (root / path).write_text(text.replace(old, new))
    stamp = root / 'output/config/target'
    stamp.parent.mkdir(parents=True)
    stamp.write_text('ppc-powermac\n')
    (root / 'spy').write_text('#!/bin/sh\necho called >> calls\ntouch product\nexit 0\n')
    (root / 'spy').chmod(0o755)
    tools = root / 'cross/bin'
    tools.mkdir(parents=True)
    for name in ('powerpc-elf-gcc', 'powerpc-elf-ld', 'powerpc-elf-ar', 'powerpc-elf-objcopy',
                 'i386-elf-gcc', 'i386-elf-ld', 'i386-elf-ar', 'i386-elf-objcopy', 'nasm'):
        (tools / name).symlink_to(root / 'spy')


def first_failure(changes=()):
    with tempfile.TemporaryDirectory(prefix='sep5-sdk-') as directory:
        root = Path(directory)
        fixture(root, changes)
        try:
            checks(root)
        except Failed as failed:
            return failed.args
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    tree_agrees()
    failure = first_failure()
    assert failure is None, failure
    print('GREEN x86 profile = make = tree; ppc and unknown targets refused (status 3, no '
          'stdout, no tool, no output); overrides, ELF mismatch and broken queries rejected',
          flush=True)
    if args.mutate:
        for name, path, old, new, label in MUTATIONS:
            failure = first_failure([(path, old, new)])
            assert failure is not None and failure[0] == label, (name, label, failure)
            print('MUTATE runtime RED %s (%s)' % (name, label), flush=True)
        assert first_failure() is None


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_sep5_sdk.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
