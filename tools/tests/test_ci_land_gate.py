#!/usr/bin/env python3
"""Integration push and document-only remerge evidence (PARALLEL_BRANCHES §3)."""
CHECK = dict(name='ci-land-gate-host', tier=1, needs=[], budget=15,
             covers=['tools/ci/land_gate.py', 'tools/tests/test_ci_land_gate.py',
                     'tools/tests/fixtures/ci-land-rerun.json',
                     '.github/workflows/check.yml', '.github/workflows/build.yml'],
             basis=['docs/tasks/agents/PARALLEL_BRANCHES.md §3'])

import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import types
import tempfile
from unittest.mock import patch
from urllib.parse import urlsplit, parse_qs

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'tools/ci/land_gate.py'
G = 'a' * 40
M = 'b' * 40


def check(source):
    tool = types.ModuleType('land_gate')
    exec(compile(source, str(TOOL), 'exec'), tool.__dict__)
    fixture = json.loads((ROOT / 'tools/tests/fixtures/ci-land-rerun.json').read_text())
    run = fixture['run']
    key = run['id'], run['run_attempt']
    expected = {name: False for name in ['static-check', 'ppc-compile', *[f'ppc-qemu ({i})' for i in range(5)]]}
    assert not tool.verdict([run], {key: fixture['2']}, expected)
    previous = {job['name']: job for job in fixture['1']}
    for job in fixture['2']:
        old = previous[job['name']]
        assert job['id'] != old['id']
        if old['conclusion'] == 'success':
            assert job['started_at'] == old['started_at']
    scenario = ''
    sha = G
    ident = 1

    def api(endpoint):
        nonlocal sha, ident
        url = urlsplit(endpoint)
        if parse_qs(url.query).get("page") == ["2"]:
            return dict(total_count=2, jobs=[dict(id=99, run_id=10, run_attempt=2, status="completed", name="extra", conclusion="success")])
        if url.path.endswith('.yml'):
            ident = 1 if 'check.yml' in url.path else 2
            return dict(id=ident)
        if url.path.endswith('/runs'):
            sha = parse_qs(url.query)['head_sha'][0]
            row = dict(id=10, run_attempt=2, head_repository=dict(full_name='repo'),
                       head_sha=sha, workflow_id=ident, event='push', status='completed',
                       conclusion='success', head_branch='wt/docs' if sha == M else 'wt/int-x')
            for field, value in dict(head_repository=dict(full_name='fork'), head_sha=M,
                                     workflow_id=30, event='workflow_dispatch', status='queued',
                                     conclusion='cancelled', head_branch='wt/stage').items():
                if scenario == field:
                    row[field] = value
            return dict(total_count=1, workflow_runs=[row])
        assert url.path.endswith('/runs/10/attempts/2/jobs')
        names = ['static-check', 'worker (0)', 'worker (1)'] if ident == 1 else ['artifact']
        jobs = [dict(id=i+1, run_id=10, run_attempt=2, status='completed', name=n,
                     conclusion='success') for i, n in enumerate(names)]
        if sha == M:
            jobs = jobs[:1] if ident == 1 else []
        if ident == 1 and sha == G and scenario != 'conditional missing':
            jobs.append(dict(id=8, run_id=10, run_attempt=2, status='completed', name='optional',
                             conclusion='failure' if scenario == 'conditional failure' else 'skipped'))
        if scenario == 'docs missing' and sha == M:
            jobs.pop(0)
        if scenario == 'missing':
            jobs.pop()
        if scenario in ('skipped', 'cancelled'):
            jobs[0]['conclusion'] = scenario
        if scenario == 'attempt':
            jobs[0]['run_attempt'] = 1
        if scenario == 'unrelated':
            jobs[0]['run_id'] = 11
        if scenario == 'running job':
            jobs[0]['status'] = 'queued'
        if scenario == 'api':
            raise subprocess.TimeoutExpired('gh', 30)
        if scenario == 'page' and ident == 2:
            return dict(total_count=2, jobs=jobs[:1])
        return dict(total_count=len(jobs), jobs=jobs)

    def command(*args):
        if args[1] == 'diff':
            return b'docs/file\0.claude/config\0README.md\0' + (b'old.c\0' if scenario == 'code' else b'')
        assert args[:2] == ('git', 'show') and args[2].split(':')[0] in (G, M)
        if args[2].endswith('build.yml'):
            return b'jobs: {artifact: {}}'
        return b"jobs: {static-check: {}, worker: {strategy: {matrix: {n: [0, 1]}}}, optional: {if: false}, cache: {if: \"github.ref == 'refs/heads/main'\"}}"

    with patch.object(tool, 'command', command), patch.object(tool, 'api', api), patch.dict(
            os.environ, GITHUB_REPOSITORY='repo'), contextlib.redirect_stderr(io.StringIO()):
        for scenario in ('', 'via', 'head_repository', 'head_sha', 'workflow_id', 'event', 'status',
                         'conclusion', 'head_branch', 'missing', 'skipped', 'cancelled', 'attempt',
                         'unrelated', 'running job', 'conditional missing', 'conditional failure', 'page', 'api', 'code', 'docs missing'):
            args = [M, '--via', G] if scenario in ('via', 'code', 'docs missing') else [G]
            with patch.object(sys, 'argv', ['land_gate.py', *args]):
                assert tool.main() == int(scenario not in ('', 'via')), scenario


MUTANTS = [
    ("run['head_repository']['full_name'] == repo", 'True'),
    ("run['head_sha'] == sha", 'True'), ("number(run['workflow_id']) == workflow", 'True'),
    ("run['event'] == 'push'", 'True'), ("run['head_branch'].startswith(prefix)", 'True'),
    ("run['status'] == 'completed'", 'True'), ("run['conclusion'] == 'success'", 'True'),
    ("number(job['run_attempt']) != key[1]", 'False'), ("number(job['run_id']) != key[0]", 'False'),
    ("job['conclusion'] == 'success'", 'True'), ("job['status'] == 'completed'", 'True'),
    ('if not missing:', 'if success:'), ("str(job.get('if', ''))", "''"),
    ("names[label] = 'if' in job", 'names[label] = False'),
    ("and job['conclusion'] == 'skipped'", ''),
    ("'wt/' if docs else 'wt/int-'", "'wt/int-'"),
    ("{'static-check': False} if docs else expected_jobs(sha, workflow)", "expected_jobs(sha, workflow)"),
    ("('check.yml',) if docs else ('check.yml', 'build.yml')", "('check.yml', 'build.yml')"),
    ('len(batch) != min(100, total - len(rows))', 'False'),
    ('or p.endswith(b\'.md\')', 'or True'), ('failures.extend(verify(repo, args.sha, docs=True))', 'pass'),
    ('failures = [str(exc)]', 'failures = []'),
]


def cli_success():
    sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    endpoints = {}
    for ident, (workflow, names) in enumerate((
            ('check.yml', ['static-check', 'ppc-compile', 'ppc-layout-inventory'] + [f'ppc-qemu ({i})' for i in range(20)]),
            ('build.yml', ['artifacts'] + [f'checks ({i})' for i in range(6)])), start=1):
        base = 'repos/repo/actions'
        endpoints[f'{base}/workflows/{workflow}'] = {'id': ident}
        row = dict(id=ident, run_attempt=2, head_repository=dict(full_name='repo'), head_sha=sha,
                   workflow_id=ident, event='push', status='completed', conclusion='success',
                   head_branch='wt/int-test')
        endpoints[f'{base}/workflows/{ident}/runs'] = dict(total_count=1, workflow_runs=[row])
        jobs = [dict(id=i, run_id=ident, run_attempt=2, status='completed', conclusion='success',
                     name=name) for i, name in enumerate(names, start=1)]
        endpoints[f'{base}/runs/{ident}/attempts/2/jobs'] = dict(total_count=len(jobs), jobs=jobs)
    with tempfile.TemporaryDirectory() as directory:
        gh = Path(directory) / 'gh'
        gh.write_text("#!/usr/bin/env python3\nimport json, os, sys\n"
                      "assert sys.argv[1] == 'api'\n"
                      "print(json.dumps(json.loads(os.environ['RESPONSES'])[sys.argv[2].split('?')[0]]))\n")
        gh.chmod(0o755)
        env = dict(os.environ, PATH=directory + os.pathsep + os.environ['PATH'],
                   RESPONSES=json.dumps(endpoints), GITHUB_REPOSITORY='repo')
        subprocess.run([sys.executable, '-B', str(TOOL), sha], cwd=ROOT, env=env, check=True)


def run(ctx):
    cli_success()
    source = TOOL.read_text()
    check(source)
    result = subprocess.run([sys.executable, '-B', str(TOOL), M, '--via', 'invalid'],
                            capture_output=True)
    assert result.returncode == 1 and b'full commit SHA' in result.stderr
    if ctx.mutate:
        for old, new in MUTANTS:
            assert source.count(old) == 1, old
            try:
                check(source.replace(old, new))
            except AssertionError:
                print('runtime RED:', old)
            else:
                raise AssertionError('survived: ' + old)
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    sys.exit(run(parser.parse_args()))
