#!/usr/bin/env python3
"""CI job independence and early artifact downloads, without GitHub access."""
CHECK = dict(name='ci-split-host', tier=0,
             covers=['.github/workflows/check.yml', '.github/workflows/build.yml', '.github/workflows/full-check.yml',
                     '.github/actions/build-setup/action.yml',
                     'tools/ci_fetch.sh', 'tools/ci/reuse_success.py', 'tools/checks/weights.json',
                     'tools/checks/run.py', 'tools/tests/test_ci_split.py',
                     'CLAUDE.md', 'README.md', 'docs/INDEX.md'],
             needs=[], budget=5.3)

import json
import os
import re
import shlex
from pathlib import Path
import subprocess
import sys
import tempfile
import importlib.util
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / '.github/workflows/build.yml'
CHECK_WORKFLOW = ROOT / '.github/workflows/check.yml'
FULL_CHECK = ROOT / '.github/workflows/full-check.yml'
SETUP = ROOT / '.github/actions/build-setup/action.yml'
FETCH = ROOT / 'tools/ci_fetch.sh'
SHA = 'a' * 40


def workflow_cases(source):
    workflow = yaml.safe_load(source)
    jobs = workflow['jobs']
    assert set(jobs) == {'artifacts', 'check-fast'}, 'artifact job and check matrix'
    for name, job in jobs.items():
        assert not job.get('needs'), 'jobs must run independently'
        runner = job['runs-on']
        assert runner == 'ubuntu-latest', 'GitHub runner (substream not used, user decision 2026-11-05)'
        assert job['timeout-minutes'] == 150
        assert job['concurrency']['cancel-in-progress'] is True
        suffix = '-${{ matrix.shard }}' if name == 'check-fast' else ''
        assert job['concurrency']['group'] == 'build-${{ github.ref }}-' + name + suffix, 'separate concurrency groups'
        steps = job['steps']
        assert any(step.get('uses') == './.github/actions/build-setup' for step in steps)
        checks = [s for s in steps if s.get('name') == 'make check-fast']
        uploads = [s for s in steps if s.get('uses') == 'actions/upload-artifact@v4']
        assert bool(checks) == (name == 'check-fast'), 'check placement'
        if name == 'check-fast':
            assert job['strategy'] == {'fail-fast': False, 'matrix': {'shard': [0, 1, 2, 3, 4, 5]}}, 'all six shards must finish'
            assert checks[0]['env']['OS32_CHECK_CACHE'] == '${{ runner.temp }}/check-cache', 'job-local result cache'
            assert 'CHECK_SHARD=${{ matrix.shard }}/6' in checks[0]['run'], 'make receives shard'
            assert not any('check-cache' in str(s.get('with', {})) for s in steps), 'result cache never persists'
            # 検査ごとの実時間だけを上げる (budget の棚卸、2026-11-08)。
            assert [u['with']['name'] for u in uploads] == ['check-times-${{ github.sha }}-${{ matrix.shard }}'], 'check-fast uploads only check times'
            reuse_gate_cases(workflow, 'build.yml', name)
            assert not any(' all ' in step.get('run', '') for step in steps), 'prepare selected needs only'
        else:
            assert not any('prior' in str(step) for step in steps), 'artifacts always built on main'
            assert any(step.get('name') == 'Build core (make all)' for step in steps)
            assert bool(uploads) == (name == 'artifacts'), 'upload placement'
    setup = yaml.safe_load(SETUP.read_text())
    assert setup['runs']['using'] == 'composite'
    steps = setup['runs']['steps']
    assert all(s.get('shell') == 'bash' for s in steps if 'run' in s)
    assert {s.get('name') for s in steps} >= {'Host dependencies',
        'Rust toolchain (rust-toolchain.toml)', 'Cache i386-elf toolchain',
        'Cache IPAex fonts', 'Fetch JF Dot Shinonome16 (verify cached font and license)'}


def reuse_gate_cases(workflow, filename, job):
    assert workflow['permissions']['actions'] == 'read', 'API token permissions'
    steps = workflow['jobs'][job]['steps']
    gate = next(i for i, s in enumerate(steps) if s.get('id') == 'prior')
    assert gate == 1 and steps[0]['uses'] == 'actions/checkout@v4', 'checkout before gate'
    assert steps[gate]['env'] == {'GH_TOKEN': '${{ github.token }}', 'CI_WORKFLOW': filename}
    assert steps[gate]['run'] == 'python3 -B tools/ci/reuse_success.py'
    for step in steps[gate + 1:]:
        assert step.get('if') in ("steps.prior.outputs.reuse != 'true'",
                                  "always() && steps.prior.outputs.reuse != 'true'"), 'all expensive steps gated'


def reuse_cases(path=None):
    spec = importlib.util.spec_from_file_location('reuse', path or ROOT / 'tools/ci/reuse_success.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    env = dict(GITHUB_EVENT_NAME='push', GITHUB_REF='refs/heads/main', GITHUB_SHA=SHA,
               GITHUB_REPOSITORY='fixture/repo', GITHUB_RUN_ID='99', CI_WORKFLOW='build.yml')
    good = dict(head_sha=SHA, workflow_id=7, status='completed', conclusion='success',
                event='push', head_branch='wt/int-test', head_repository=dict(full_name='fixture/repo'), id=42)
    for workflow in ('build.yml', 'check.yml'):
        with patch.object(module, 'api', side_effect=[{'id': 7}, {'workflow_runs': [good]}]) as api:
            assert module.reusable(dict(env, CI_WORKFLOW=workflow)), 'exact successful integration push'
            assert api.call_args_list[0].args[0].endswith('/' + workflow)
            assert 'head_sha=' + SHA in api.call_args_list[1].args[0]
    for field, bad in [('head_sha', 'a'*39+'b'), ('workflow_id', 8), ('status', 'in_progress'),
                       ('conclusion', 'failure'), ('conclusion', 'skipped'), ('event', 'pull_request'),
                       ('head_branch', 'main'), ('head_branch', 'wt/feature'),
                       ('head_repository', dict(full_name='fork/repo')), ('id', 99)]:
        with patch.object(module, 'api', side_effect=[{'id': 7}, {'workflow_runs': [dict(good, **{field: bad})]}]):
            assert not module.reusable(env), 'reject mismatch: ' + field
    for field, bad in [('GITHUB_EVENT_NAME', 'workflow_dispatch'), ('GITHUB_EVENT_NAME', 'pull_request'),
                       ('GITHUB_REF', 'refs/heads/wt/int-test'), ('GITHUB_SHA', 'aaaa'), ('CI_WORKFLOW', 'other.yml')]:
        with patch.object(module, 'api', side_effect=AssertionError('unexpected API')):
            assert not module.reusable(dict(env, **{field: bad})), 'only main push eligible'
    failures = [OSError('offline'), subprocess.TimeoutExpired('gh', 20),
                subprocess.CalledProcessError(1, 'gh'), ValueError('bad JSON')]
    for response in ({'workflow_runs': []}, {}, {'workflow_runs': [{}]}, {'workflow_runs': None}):
        failures.append([{'id': 7}, response])
    for failure in failures:
        with tempfile.TemporaryDirectory(prefix='reuse-') as tmp:
            output = Path(tmp) / 'output'
            with patch.dict(os.environ, dict(env, GITHUB_OUTPUT=str(output)), clear=True), \
                    patch.object(module, 'api', side_effect=failure):
                assert module.main() == 0
            assert output.read_text() == 'reuse=false\n', 'errors and ambiguity execute checks'


# These checks require build artifacts, unavailable in the cross-free static job.
DOC_EXCLUSIONS = {
    'check-surface-lease-host': 'needs make:all',
    'check-surface-bundle-host': 'needs make:all',
    'check-manifests': 'needs make:all',
    'check-deploy-set-host': 'needs make:all',
    'check-disk-auth-host': 'needs make:all',
    'check-ci-stab-host': 'needs make:all',
}


def document_cases(source):
    workflow = yaml.safe_load(source)
    reuse_gate_cases(workflow, 'check.yml', 'static-check')
    runs = [step.get('run', '') for job in workflow['jobs'].values()
            for step in job['steps']]
    targets = set(re.findall(r'^\s*make\s+(check-[\w-]+)\s*$', '\n'.join(runs), re.M))
    assert not {'check-arch-asm', 'check-le-access'} & targets, 'full AST runs in build only'
    assert all(DOC_EXCLUSIONS.values()), 'exclusions require reasons'
    for paths in (('docs/INDEX.md',), ('CLAUDE.md',), ('README.md',),
                  ('docs/INDEX.md', '.claude/skills/os32-land/SKILL.md')):
        result = subprocess.run([sys.executable, '-B', 'tools/checks/run.py',
                                 '--select', '--files', *paths], cwd=ROOT,
                                capture_output=True, text=True, check=True)
        line = next(line for line in result.stdout.splitlines() if line.startswith('CC_STAGE1='))
        selected = set(shlex.split(line.split('=', 1)[1])[0].split())
        missing = selected - targets - DOC_EXCLUSIONS.keys()
        assert not missing, f'{paths}: undocumented checks {sorted(missing)}'


def full_check_cases(source):
    workflow = yaml.safe_load(source)
    # PyYAML's YAML 1.1 loader reads the GitHub Actions key "on" as True.
    triggers = workflow[True] if True in workflow else workflow['on']
    assert set(triggers) == {'workflow_dispatch', 'schedule'}, 'full-check triggers'
    assert triggers['workflow_dispatch']['inputs']['ref']['default'] == 'main'
    assert triggers['schedule'] == [{'cron': '0 18 * * *'}]
    assert workflow['concurrency'] == dict(group='full-check', **{'cancel-in-progress': False})
    job, = workflow['jobs'].values()
    assert job['runs-on'] == 'ubuntu-latest'
    assert job['timeout-minutes'] == 330
    steps = job['steps']
    checkout = next(s for s in steps if s.get('uses') == 'actions/checkout@v4')
    assert checkout['with']['ref'] == "${{ github.event_name == 'workflow_dispatch' && inputs.ref || 'main' }}"
    setup = next(i for i, s in enumerate(steps) if s.get('uses') == './.github/actions/build-setup')
    build = next(i for i, s in enumerate(steps) if s.get('name') == 'Build core (make all)')
    check = next(i for i, s in enumerate(steps) if s.get('name') == 'make check')
    assert setup < build < check, 'prepare, build, then check'
    assert ' all ' in steps[build]['run']
    assert 'make check ' in steps[check]['run'] and 'MUTATE=1' in steps[check]['run'], 'full mutation check'
    upload = next(s for s in steps if s.get('uses') == 'actions/upload-artifact@v4')
    assert upload['if'] == 'always()' and upload['with']['retention-days'] == 14
    collect = next(s for s in steps if s.get('name') == 'Collect check results')
    assert collect['if'] == 'always()'
    # Inspect workflow text only; this test does not read generated timing files.
    assert 'check_times.tsv' in collect['run'] and 'failed-checks.txt' in collect['run']


# This stub is the only gh reachable through PATH; reject unexpected calls.
GH = '''#!/usr/bin/env python3
import hashlib, json, os
from pathlib import Path
import sys
args = sys.argv[1:]
root = Path(os.environ['FIXTURE'])
with (root / 'calls').open('a') as out:
    out.write(json.dumps(args) + '\\n')
if args[:2] == ['auth', 'status']:
    sys.exit(0)
if args[:2] == ['run', 'list']:
    assert '--status' not in args, 'must not filter whole-run status'
    assert args[args.index('--workflow') + 1] == 'build.yml'
    print(json.dumps([dict(databaseId=42, headSha='a'*40, headBranch='wt/probe', createdAt='now')]))
elif args[0] == 'api' and '/jobs' in args[1]:
    assert args[1] == 'repos/fixture/repo/actions/runs/42/jobs?per_page=100'
    state = os.environ['FIXTURE_ART_STATE']
    if state == 'wait-success':
        state = 'success' if (root / 'slept').exists() else 'in_progress'
    checks = ([dict(name='check-fast (' + str(i) + ')', status='completed', conclusion=s)
           for i, s in enumerate(json.loads(os.environ['FIXTURE_SHARDS']))]
          if 'FIXTURE_SHARDS' in os.environ else
          [dict(name='check-fast', status='in_progress', conclusion=os.environ.get('FIXTURE_CHECK_STATE'))])
    print(json.dumps({'jobs': [dict(name='artifacts', status='completed' if state in ('success', 'failure') else state,
                                   conclusion=state if state in ('success', 'failure') else None)] + checks}))
elif args[0] == 'api' and args[1] == 'repos/fixture/repo/actions/runs/42/artifacts':
    print('os32-wt-probe-aaaaaaa')
elif args[:2] == ['run', 'download']:
    assert args[2] == '42'
    dest = Path(args[args.index('-D') + 1])
    (root / 'downloaded').touch()
    (dest / 'BUILD_INFO.txt').write_text('fixture build\\n')
    (dest / 'payload').write_bytes(b'fixture')
    (dest / 'SHA256SUMS').write_text(hashlib.sha256(b'fixture').hexdigest() + '  payload\\n')
else:
    raise AssertionError(args)
'''


def fetch_case(script, state, flags=(), check=None, shard_states=None):
    with tempfile.TemporaryDirectory(prefix='ci_fetch_') as tmp:
        root = Path(tmp)
        gh = root / 'gh'
        gh.write_text(GH.replace('#!/usr/bin/env python3', '#!' + sys.executable, 1))
        gh.chmod(0o755)
        sleep = root / 'sleep'
        sleep.write_text('#!/bin/sh\ntouch "$FIXTURE/slept"\n')
        sleep.chmod(0o755)
        env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                   FIXTURE=str(root), FIXTURE_ART_STATE=state)
        if shard_states is not None:
            env['FIXTURE_SHARDS'] = json.dumps(shard_states)
        if check:
            env['FIXTURE_CHECK_STATE'] = check
        else:
            env.pop('FIXTURE_CHECK_STATE', None)
        result = subprocess.run(['bash', str(script), '--repo', 'fixture/repo',
                                 '--sha', SHA, '--dir', str(root / 'out'), *flags],
                                capture_output=True, text=True, env=env, timeout=5)
        calls = [json.loads(line) for line in (root / 'calls').read_text().splitlines()]
        return result, (root / 'downloaded').exists(), (root / 'slept').exists(), calls


def fetch_cases(script):
    for check in (None, 'failure', 'success'):
        result, downloaded, _, _ = fetch_case(script, 'success', check=check)
        assert result.returncode == 0 and downloaded, 'successful artifacts must download: ' + result.stderr
        assert ('検査は失敗 (または実行中)' in result.stderr) == (check != 'success'), 'check status notice'
    for states in (['success'] * 6, *[(['success'] * i + ['failure'] + ['success'] * (5-i))
                                               for i in range(6)], ['success'] * 5,
                   ['success'] * 5 + ['in_progress'], ['success'] * 5 + ['cancelled']):
        result, downloaded, _, _ = fetch_case(script, 'success', shard_states=states)
        assert result.returncode == 0 and downloaded, result.stderr
        assert ('検査は失敗 (または実行中)' in result.stderr) == (states != ['success'] * 6), 'all shards required for success notice'
    for state in ('in_progress', 'failure'):
        result, downloaded, slept, calls = fetch_case(script, state)
        assert result.returncode == 1 and not downloaded and not slept, 'unfinished/failed artifacts must not download'
        assert not any(c[0] == 'api' and '/artifacts' in c[1] for c in calls), 'no artifact lookup before success'
        if state == 'in_progress':
            assert 'artifacts のジョブを待っている' in result.stderr
    result, downloaded, slept, _ = fetch_case(script, 'wait-success', ('--wait',))
    assert result.returncode == 0 and downloaded and slept, 'wait then download'
    result, downloaded, slept, _ = fetch_case(script, 'failure', ('--wait',))
    assert result.returncode == 1 and not downloaded and not slept, 'failed job must not wait forever'
    result, downloaded, _, _ = fetch_case(script, 'success', ('--dry-run',))
    assert result.returncode == 0 and not downloaded and 'dry-run:' in result.stdout, 'dry-run'


def main():
    reuse_cases()
    source = WORKFLOW.read_text()
    workflow_cases(source)
    full_check_cases(FULL_CHECK.read_text())
    document_cases(CHECK_WORKFLOW.read_text())
    fetch_cases(FETCH)
    print('ci-split: workflow and fetch cases GREEN', flush=True)
    if '--mutate' in sys.argv:
        mutations = [
            ('reuse', "r['head_sha'] == sha", 'True', 'reuse wrong SHA'),
            ('reuse', "r['workflow_id'] == workflow_id", 'True', 'reuse wrong workflow'),
            ('reuse', "r['conclusion'] == 'success'", 'True', 'reuse failed run'),
            ('reuse', "r['head_branch'].startswith('wt/int-')", 'True', 'reuse non integration run'),
            ('reuse', '        reuse = False', '        reuse = True', 'reuse on API failure'),
            ('check', '          make check-docs-tree\n', '', 'missing docs tree'),
            ('workflow', 'shard: [0, 1, 2, 3, 4, 5]', 'shard: [0, 1, 2, 3, 4]', 'lose shard'),
            ('workflow', 'fail-fast: false', 'fail-fast: true', 'cancel sibling shards'),
            ('workflow', 'CHECK_SHARD=${{ matrix.shard }}/6', 'CHECK_SHARD=0/6', 'repeat first shard'),
            ('workflow', 'name: check-times-${{ github.sha }}-${{ matrix.shard }}', 'name: check-times-${{ github.sha }}', 'artifact collision'),
            ('workflow', 'OS32_CHECK_CACHE: ${{ runner.temp }}/check-cache', 'OS32_CHECK_CACHE: off', 'disable shared cache'),
            ('fetch', 'all(s == "success" for s in states)', 'any(s == "success" for s in states)', 'accept partial shards'),
            ('workflow', '  check-fast:\n', '  check-fast:\n    needs: artifacts\n', 'serialize jobs'),
            ('workflow', '    runs-on: ubuntu-latest', '    runs-on: [self-hosted, substream]', 'route to substream'),
            ('full-check', 'MUTATE=1', 'MUTATE=0', 'disable full-check mutations'),
            ('fetch', 'if [ "$ART_STATE" = success ]; then', 'if [ "$ART_STATE" != success ]; then', 'invert artifact gate'),
            ('fetch', 'if [ "$CHECK_STATE" != success ]; then', 'if [ "$CHECK_STATE" = success ]; then', 'invert check notice'),
        ]
        for kind, old, new, name in mutations:
            text = {'workflow': source, 'full-check': FULL_CHECK.read_text(), 'fetch': FETCH.read_text(), 'check': CHECK_WORKFLOW.read_text(), 'reuse': (ROOT / 'tools/ci/reuse_success.py').read_text()}[kind]
            # Runner occurs once per job; mutate only the check-fast job.
            if name == 'route to substream':
                prefix, text = text.split('  check-fast:\n', 1)
                assert text.count(old) == 1, name
                changed = prefix + '  check-fast:\n' + text.replace(old, new)
            else:
                assert text.count(old) == 1, name
                changed = text.replace(old, new)
            with tempfile.TemporaryDirectory(prefix='ci_mut_') as tmp:
                script = Path(tmp) / ('reuse.py' if kind == 'reuse' else 'ci_fetch.sh')
                script.write_text(changed)
                if kind == 'fetch':
                    subprocess.run(['bash', '-n', str(script)], check=True)
                try:
                    if kind == 'reuse':
                        reuse_cases(script)
                    elif kind == 'workflow':
                        workflow_cases(changed)
                    elif kind == 'check':
                        document_cases(changed)
                    elif kind == 'full-check':
                        full_check_cases(changed)
                    else:
                        fetch_cases(script)
                except AssertionError as exc:
                    print('MUTATION RED: ' + name + ': ' + str(exc).splitlines()[0])
                else:
                    raise AssertionError('mutation survived: ' + name)
        print(f'ci-split: {len(mutations)} runtime RED / 0 survived / 0 ERROR')
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', str(Path(ctx.root) / 'tools/tests/test_ci_split.py'),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
