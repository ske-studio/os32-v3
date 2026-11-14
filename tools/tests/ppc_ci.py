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
    draft = (ROOT / 'drafts/ci-ppc-cross/workflows.patch').read_text()
    assert 'a/.github/workflows/build.yml' not in draft
    assert draft.count('+      - uses: ./.github/actions/ppc-setup') == 1
    assert '+  ppc-compile:' in draft
    action = yaml.safe_load((ROOT / '.github/actions/ppc-setup/action.yml').read_text())
    commands = '\n'.join(step.get('run', '') for step in action['runs']['steps'])
    assert 'sha256sum tools/ci/build_cross_ppc.sh' in commands
    assert 'tools/ci/build_cross.sh' not in commands
    assert 'OS32_REQUIRE_PPC=1' in commands


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    matrix(source)
    configure()
    wiring()
    if args.mutate:
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
