#!/usr/bin/env python3
"""CI job independence and early artifact downloads, without GitHub access."""
CHECK = dict(name='ci-split-host', tier=0,
             covers=['.github/workflows/build.yml', '.github/workflows/full-check.yml',
                     '.github/actions/build-setup/action.yml',
                     'tools/ci_fetch.sh', 'tools/tests/test_ci_split.py'],
             needs=[], budget=3)

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / '.github/workflows/build.yml'
FULL_CHECK = ROOT / '.github/workflows/full-check.yml'
SETUP = ROOT / '.github/actions/build-setup/action.yml'
FETCH = ROOT / 'tools/ci_fetch.sh'
SHA = 'a' * 40


def workflow_cases(source):
    workflow = yaml.safe_load(source)
    jobs = workflow['jobs']
    assert set(jobs) == {'artifacts', 'check-fast'}, 'two build jobs'
    prefixes = []
    for name, job in jobs.items():
        assert not job.get('needs'), 'jobs must run independently'
        runner = job['runs-on']
        assert runner == 'ubuntu-latest', 'GitHub runner (substream not used, user decision 2026-11-05)'
        assert job['timeout-minutes'] == 150
        assert job['concurrency']['cancel-in-progress'] is True
        assert job['concurrency']['group'] == 'build-${{ github.ref }}-' + name, 'separate concurrency groups'
        steps = job['steps']
        core = next(i for i, step in enumerate(steps) if step.get('name') == 'Build core (make all)')
        prefixes.append(steps[:core + 1])
        assert any(step.get('uses') == './.github/actions/build-setup' for step in steps[:core])
        checks = [s for s in steps if s.get('name') == 'make check-fast']
        uploads = [s for s in steps if s.get('uses') == 'actions/upload-artifact@v4']
        assert bool(checks) == (name == 'check-fast'), 'check placement'
        if name == 'check-fast':
            # 検査ごとの実時間だけを上げる (budget の棚卸、2026-11-08)。
            assert [u['with']['name'] for u in uploads] == ['check-times-${{ github.sha }}'], 'check-fast uploads only check times'
        else:
            assert bool(uploads) == (name == 'artifacts'), 'upload placement'
    assert prefixes[0] == prefixes[1], 'identical preparation and build'
    setup = yaml.safe_load(SETUP.read_text())
    assert setup['runs']['using'] == 'composite'
    steps = setup['runs']['steps']
    assert all(s.get('shell') == 'bash' for s in steps if 'run' in s)
    assert {s.get('name') for s in steps} >= {'Host dependencies',
        'Rust toolchain (rust-toolchain.toml)', 'Cache i386-elf toolchain',
        'Cache IPAex fonts', 'Fetch JF Dot Shinonome16 (verify cached font and license)'}


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
    print(json.dumps({'jobs': [dict(name='artifacts', status='completed' if state in ('success', 'failure') else state,
                                   conclusion=state if state in ('success', 'failure') else None),
                              dict(name='check-fast', status='in_progress', conclusion=os.environ.get('FIXTURE_CHECK_STATE'))]}))
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


def fetch_case(script, state, flags=(), check=None):
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
    source = WORKFLOW.read_text()
    workflow_cases(source)
    full_check_cases(FULL_CHECK.read_text())
    fetch_cases(FETCH)
    print('ci-split: workflow and fetch cases GREEN', flush=True)
    if '--mutate' in sys.argv:
        mutations = [
            ('workflow', '  check-fast:\n', '  check-fast:\n    needs: artifacts\n', 'serialize jobs'),
            ('workflow', '    runs-on: ubuntu-latest', '    runs-on: [self-hosted, substream]', 'route to substream'),
            ('full-check', 'MUTATE=1', 'MUTATE=0', 'disable full-check mutations'),
            ('fetch', 'if [ "$ART_STATE" = success ]; then', 'if [ "$ART_STATE" != success ]; then', 'invert artifact gate'),
            ('fetch', 'if [ "$CHECK_STATE" != success ]; then', 'if [ "$CHECK_STATE" = success ]; then', 'invert check notice'),
        ]
        for kind, old, new, name in mutations:
            text = {'workflow': source, 'full-check': FULL_CHECK.read_text(), 'fetch': FETCH.read_text()}[kind]
            # Runner occurs once per job; mutate only the check-fast job.
            if name == 'route to substream':
                prefix, text = text.split('  check-fast:\n', 1)
                assert text.count(old) == 1, name
                changed = prefix + '  check-fast:\n' + text.replace(old, new)
            else:
                assert text.count(old) == 1, name
                changed = text.replace(old, new)
            with tempfile.TemporaryDirectory(prefix='ci_mut_') as tmp:
                script = Path(tmp) / 'ci_fetch.sh'
                script.write_text(changed)
                if kind == 'fetch':
                    subprocess.run(['bash', '-n', str(script)], check=True)
                try:
                    if kind == 'workflow':
                        workflow_cases(changed)
                    elif kind == 'full-check':
                        full_check_cases(changed)
                    else:
                        fetch_cases(script)
                except AssertionError as exc:
                    print('MUTATION RED: ' + name + ': ' + str(exc).splitlines()[0])
                else:
                    raise AssertionError('mutation survived: ' + name)
        print('ci-split: 5 runtime RED / 0 survived / 0 ERROR')
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', str(Path(ctx.root) / 'tools/tests/test_ci_split.py'),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
