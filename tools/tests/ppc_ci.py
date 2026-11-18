#!/usr/bin/env python3
"""Exercise the actual stage 1 gate and configure argument branches."""
import argparse
import re
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
                assert build.call_count == (len(module.IMAGES) + 1 if present else 0)
                if present:
                    # Expectation origin: PPC_P5_REST_DESIGN §3 P6 (all four images explicit).
                    built = [c.args[0][3:-1] for c in build.call_args_list]
                    assert built == [list(f) for f in module.IMAGES] + [module.COMMON_COMPILE], built
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
    # ppc-compile と、main のキャッシュを保つ ppc-toolchain-cache。ppc-qemu は gate-setup の中で呼ぶ。
    assert check.count('- uses: ./.github/actions/ppc-setup') == 2
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
    # Origin: mainline PM's PACK.md decision, documented in docs/TESTS.md.
    command = 'python3 -B tools/checks/run.py --run all --env ppc-cross --exclude-env qemu-ppc'
    for job_steps, routine, mutate in ((steps, False, 1),):   # 関門は変異込み (docs/POLICY_CI.md G1)
        runs = '\n'.join(step.get('run', '') for step in job_steps)
        assert command in runs
        assert f'MUTATE={mutate} ' + command in runs
        assert ('CHECK_ROUTINE=1 ' in runs) == routine
        assert 'make check-ppc-compile' not in runs
        assert any(step.get('uses') == './.github/actions/build-setup' for step in job_steps)
        # 変異の試験が QEMU を起動するので、ppc-qemu の段と同じ道具を入れる。
        for dependency in ('gcc-13', 'qemu-user', 'ccache', 'qemu-system-ppc', 'qemu-system-data'):
            assert dependency in runs
    # QEMU の検査は 4 本に割って並べ、手本の道具 (tools/ci/gate.py) の 1 行で回す (変異込みが既定)。
    command = 'python3 -B tools/ci/gate.py run --env qemu-ppc --shard ${{ matrix.shard }}/4 --out "$RUNNER_TEMP/gate"'
    for flow, routine, mutate in ((workflow, False, 1),):
        job = flow['jobs']['ppc-qemu']
        # ppc-compile の成果物は読まないので鎖にしない (docs/POLICY_CI.md G2)。
        assert 'needs' not in job
        assert job['strategy'] == {'fail-fast': False, 'matrix': {'shard': [0, 1, 2, 3]}}
        assert job['timeout-minutes'] >= 60
        # The failure-only diagnostics step prints qemu.stderr; it is not part of the run.
        guest = [s for s in job['steps'] if s.get('name') != 'PPC QEMU diagnostics']
        assert any(s.get('name') == 'PPC QEMU diagnostics' and s.get('if') == 'failure()'
                   for s in job['steps'])
        assert guest[0] == flow['jobs']['ppc-compile']['steps'][0]
        assert guest[1] == steps[1]
        # 成果物を出す手順は always()・failure() と and で繋ぐ。範囲の条件はどの手順にも要る。
        uploads = ('Upload check times', 'Upload failed logs')
        assert all(s.get('if') == "steps.scope.outputs.scope == 'ppc'" for s in guest[2:] if s.get('name') not in uploads)
        assert all(s.get('if', '').endswith("&& steps.scope.outputs.scope == 'ppc'") for s in guest if s.get('name') in uploads)
        runs = '\n'.join(s.get('run', '') for s in guest)
        assert command in runs
        assert '--no-mutate' not in runs and 'run.py --run' not in runs
        assert ('CHECK_ROUTINE=1 ' in runs) == routine
        # 検査ごとの所要は成果物に出す (重みの作り直しに使う: tools/checks/calibrate_weights.py)。
        times = [s for s in guest if s.get('name') == 'Upload check times']
        assert times and times[0]['with']['name'] == 'check-times-${{ github.sha }}-ppc-qemu-${{ matrix.shard }}'
        assert times[0]['if'] == "always() && steps.scope.outputs.scope == 'ppc'"
        # 用意は手本と同じ gate-setup の 1 行だけ (docs/POLICY_CI.md G5)。apt の行をジョブに書かない。
        assert [s.get('uses') for s in guest if s.get('uses', '').startswith('./.github/actions/')] == ['./.github/actions/gate-setup']
        assert 'apt-get' not in runs
        gate = yaml.safe_load((ROOT / '.github/actions/gate-setup/action.yml').read_text())
        gate_uses = [s.get('uses') for s in gate['runs']['steps'] if s.get('uses')]
        assert gate_uses == ['./.github/actions/build-setup', './.github/actions/ppc-setup']
        provided = '\n'.join(s.get('run', '') for s in gate['runs']['steps'])
        provided += (ROOT / '.github/actions/build-setup/action.yml').read_text()
        # mac99 の firmware (qemu-system-data) と VGA の ROM (seabios) を名指しで。全 arch を連れてくる
        # メタパッケージ qemu-system は使わない (遅いミラーで用意が 600 秒を越えた)。
        for dependency in ('qemu-system-ppc', 'qemu-system-data', 'seabios', 'genisoimage', 'qemu-user', 'gcc-13', 'ccache'):
            assert dependency in provided, dependency
        installs = '\n'.join(line for line in provided.splitlines() if 'apt-get' in line and ' install ' in line)
        assert not re.search(r'\bqemu-system(\s|$)', installs, re.M), 'qemu-system metapackage'
    # 枝が読めるキャッシュは自分の枝と main の物だけ。main がクロスコンパイラのキャッシュを保つ。
    warm = workflow['jobs']['ppc-toolchain-cache']
    assert warm['if'] == "github.ref == 'refs/heads/main'"
    assert [s.get('uses') for s in warm['steps']] == ['actions/checkout@v4', './.github/actions/ppc-setup']
    action = yaml.safe_load((ROOT / '.github/actions/ppc-setup/action.yml').read_text())
    by_name = {step['name']: step for step in action['runs']['steps']}
    for name in ('Toolchain build dependencies (cache miss only)', 'Build powerpc-elf toolchain (cache miss only)'):
        assert by_name[name]['if'] == "steps.cache.outputs.cache-hit != 'true'"
    commands = '\n'.join(step.get('run', '') for step in action['runs']['steps'])
    assert 'sha256sum tools/ci/build_cross_ppc.sh' in commands
    assert 'tools/ci/build_cross.sh' not in commands
    assert 'OS32_REQUIRE_PPC=1' in commands


def guest_prerequisites():
    # Exercise the wrappers' missing-tool branch without building a guest.
    import importlib.util
    from types import SimpleNamespace
    for name in ('ppc-sc', 'ppc-sd', 'ppc-sh-output'):
        path = ROOT / ('tools/checks/' + name + '.py')
        spec = importlib.util.spec_from_file_location('guest_gate', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        assert module.CHECK['needs'] == ['make:ppc-ci-image']
        for required in ('0', '1'):
            with patch.dict('os.environ', {'OS32_REQUIRE_PPC': required}), \
                 patch('shutil.which', return_value=None):
                assert module.run(SimpleNamespace(root=ROOT, mutate=False)) == int(required)


def scope_matrix(source):
    module = types.ModuleType('scope_subject')
    exec(compile(source, 'ppc_scope.py', 'exec'), module.__dict__)
    for path in ('arch/ppc/entry.S', 'platform/powermac/ofw.c', 'build/targets/ppc-powermac.mk', 'build/images/ppc-flags.mk', 'build/select.mk',
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
    guest_prerequisites()
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
