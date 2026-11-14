#!/usr/bin/env python3
"""Exercise the actual stage 1 gate and configure argument branches."""
import argparse
from pathlib import Path
import shlex
import subprocess
import types

import yaml
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/ppc/compile_ci.py'


def matrix(source):
    module = types.ModuleType('ppc_ci_subject')
    exec(compile(source, str(SOURCE), 'exec'), module.__dict__)
    for required in (False, True):
        for present in (False, True):
            env = {'PPC_CROSS_DIR': '/fixture/cross'}
            if required:
                env['OS32_REQUIRE_PPC'] = '1'
            with patch.object(module.shutil, 'which', return_value='/compiler' if present else None), \
                 patch.object(module.subprocess, 'call', return_value=0) as build:
                rc = module.run(ROOT, env)
                assert rc == int(required and not present), (required, present, rc)
                assert build.call_count == int(present)
            if present:
                with patch.object(module.shutil, 'which', return_value='/compiler'), \
                     patch.object(module.subprocess, 'call', return_value=7):
                    assert module.run(ROOT, env) == 7


def configure():
    common = ['--target=i386-elf', '--prefix=/fixture/cross']
    expected = [common + ['--with-sysroot', '--disable-nls', '--disable-werror'],
                common + ['--disable-nls', '--enable-languages=c', '--without-headers'],
                common + ['--disable-multilib', '--disable-newlib-supplied-syscalls',
                          '--enable-newlib-nano-malloc', '--enable-newlib-nano-formatted-io'],
                common + ['--disable-nls', '--enable-languages=c', '--with-newlib']]
    command = ['bash', 'tools/ci/build_cross_ppc.sh', '--dry-run', '--prefix', '/fixture/cross']
    lines = subprocess.check_output(command, cwd=ROOT, text=True).splitlines()
    actual = [shlex.split(line.split('configure ', 1)[1]) for line in lines]
    want = [[arg.replace('i386-elf', 'powerpc-elf') for arg in row] for row in expected]
    want[1].append('--disable-multilib')
    want[3].append('--disable-multilib')
    assert actual == want, (actual, want)


def wiring():
    check = (ROOT / '.github/workflows/check.yml').read_text()
    build = (ROOT / '.github/workflows/build.yml').read_text()
    assert 'ppc-setup' not in build
    assert check.count('- uses: ./.github/actions/ppc-setup') == 1
    assert '  ppc-compile:' in check
    workflow = yaml.safe_load(check)
    triggers = workflow.get('on', workflow.get(True))
    arch = triggers['workflow_dispatch']['inputs']['arch']
    assert arch['default'] == 'auto'
    assert arch['options'] == ['auto', 'x86', 'ppc', 'both']
    steps = workflow['jobs']['ppc-compile']['steps']
    assert steps[0]['with']['fetch-depth'] == 0
    assert steps[1]['id'] == 'scope'
    assert steps[1]['run'] == 'python3 -B tools/ci/ppc_scope.py'
    assert steps[1]['env']['PPC_ARCH'] == "${{ github.event_name == 'workflow_dispatch' && inputs.arch || 'auto' }}"
    assert all(s.get('if') == "steps.scope.outputs.scope == 'ppc'" for s in steps[2:])
    full = yaml.safe_load((ROOT / '.github/workflows/full-check.yml').read_text())
    ppc = full['jobs']['ppc-compile']['steps']
    assert all('if' not in s for s in ppc)
    assert ppc[0]['with']['ref'] == "${{ github.event_name == 'workflow_dispatch' && inputs.ref || 'main' }}"
    assert any(s.get('uses') == './.github/actions/ppc-setup' for s in ppc)
    assert any('make check-ppc-compile MUTATE=1' in s.get('run', '') for s in ppc)
    action = yaml.safe_load((ROOT / '.github/actions/ppc-setup/action.yml').read_text())
    commands = '\n'.join(step.get('run', '') for step in action['runs']['steps'])
    assert 'sha256sum tools/ci/build_cross_ppc.sh' in commands
    assert 'tools/ci/build_cross.sh' not in commands
    assert 'OS32_REQUIRE_PPC=1' in commands


def scope_matrix(source):
    module = types.ModuleType('scope_subject')
    exec(compile(source, 'ppc_scope.py', 'exec'), module.__dict__)
    for path in ('arch/ppc/entry.S', 'platform/powermac/ofw.c', 'build/ppc-kboot.mk',
                 'userland/ppc/a.c', 'tools/ppc/compile_ci.py',
                 'tools/ci/build_cross_ppc.sh', '.github/actions/ppc-setup/action.yml',
                 'tools/ci/ppc_scope.py', '.github/workflows/check.yml',
                 'build/ram-root.mk', 'Makefile', 'tools/tests/ppc_ci.py',
                 'tools/checks/ppc-compile.py', 'tools/gen_unicode.c', 'platform/none/a.c',
                 'unclassified/a.c', 'userland/newprog/a.c',
                 *[p + 'fixture.c' for p in ('kernel/', 'lib/', 'include/', 'net/', 'fs/',
                    'mm/', 'exec/', 'gfx/', 'drivers/', 'init/', 'sdk/', 'kapi/',
                    'userland/shell/', 'assets/fonts/', 'userland/lib/')]):
        scope, reason = module.decide([path], 'wt/feature')
        assert scope == 'ppc', path
        # Known PPC/common inputs must be classified explicitly, not by the fallback.
        if path not in ('unclassified/a.c', 'userland/newprog/a.c', 'tools/gen_unicode.c'):
            assert not reason.startswith('conservative'), (path, reason)
    for paths in ([], ['arch/x86/a.c'], ['platform/pc98/a.c'],
                  ['docs/build.md', 'notes.md', 'lib/notes.md', 'tools/ppc/notes.md'], ['arch/x86/a.c', 'docs/a.md']):
        assert module.decide(paths, 'wt/feature')[0] == 'skip', paths
    for ref in ('wt/int-test', 'refs/heads/wt/int-test'):
        assert module.decide(['notes.md'], ref)[0] == 'ppc'
    for arch in ('ppc', 'both'):
        assert module.decide([], 'wt/feature', arch)[0] == 'ppc'
    scope, reason = module.decide(['kernel/a.c'], 'wt/int-test', 'x86')
    assert scope == 'skip' and 'arch=x86' in reason and 'PASS' not in reason
    with patch.object(module.subprocess, 'check_output',
                      side_effect=['base\n', b'kernel/a.c\0old.c\0']) as git:
        assert module.changed_paths() == ['kernel/a.c', 'old.c']
        assert git.call_args_list[0].args[0] == ['git', 'merge-base', 'origin/main', 'HEAD']
        assert git.call_args_list[1].args[0] == [
            'git', 'diff', '--name-only', '--no-renames', '-z', 'base', 'HEAD']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    matrix(source)
    configure()
    wiring()
    scope_source = (ROOT / "tools/ci/ppc_scope.py").read_text()
    scope_matrix(scope_source)
    if args.mutate:
        # Removing COMMON must produce a runtime wrong decision, not a syntax error.
        anchor = 'path.startswith(COMMON + PPC)'
        assert scope_source.count(anchor) == 1
        try:
            scope_matrix(scope_source.replace(anchor, 'path.startswith(PPC)'))
        except AssertionError:
            print('RED: common scope omission detected')
        else:
            raise AssertionError('scope mutant survived')
        scope_matrix(scope_source)
        anchor = "required = env.get('OS32_REQUIRE_PPC') == '1'"
        assert source.count(anchor) == 1
        try:
            matrix(source.replace(anchor, 'required = False'))
        except AssertionError:
            print('RED: ignoring OS32_REQUIRE_PPC detected')
        else:
            raise AssertionError('mutant survived')
        matrix(source)  # Post-mutation normal control.
    print('PASS: PPC CI gate matrix and configure branches')


if __name__ == '__main__':
    main()
