#!/usr/bin/env python3
"""Verify the integration push evidence for a fast-forward to main."""
import argparse
import itertools
import json
import os
import re
import subprocess
import sys
from urllib.parse import urlencode

import yaml


def command(*args):
    return subprocess.check_output(args, timeout=30)


def api(endpoint):
    return json.loads(command('gh', 'api', endpoint))


def number(value):
    if type(value) is not int or value < 1:
        raise ValueError(f'invalid GitHub identifier or attempt: {value!r}')
    return value


def pages(endpoint, field, query):
    rows = []
    seen = set()
    total = None
    for page in itertools.count(1):
        data = api(endpoint + '?' + urlencode(dict(query, per_page=100, page=page)))
        count = data['total_count']
        batch = data[field]
        if type(count) is not int or count < 0 or (total is not None and total != count):
            raise ValueError(f'{field}: invalid or changing total_count')
        total = count
        if not isinstance(batch, list) or len(batch) != min(100, total - len(rows)):
            raise ValueError(f'{field}: incomplete page {page}')
        for row in batch:
            ident = number(row['id'])
            if ident in seen:
                raise ValueError(f'{field}: repeated id {ident}')
            seen.add(ident)
        rows.extend(batch)
        if len(rows) == total:
            return rows


def expected_jobs(sha, workflow):
    jobs = yaml.safe_load(command('git', 'show', f'{sha}:.github/workflows/{workflow}'))['jobs']
    names = {}
    for name, job in jobs.items():
        if re.search(r"""==\s*(['"])refs/heads/main\1|(['"])refs/heads/main\2\s*==""", str(job.get('if', ''))):
            continue
        if 'name' in job:
            raise ValueError(f'{name}: unsupported custom job name')
        matrix = job.get('strategy', {}).get('matrix', {})
        if any(axis in ('include', 'exclude') or not isinstance(values, list) or not values
               for axis, values in matrix.items()):
            raise ValueError(f'{name}: unsupported matrix')
        for values in itertools.product(*matrix.values()):
            label = name + (' (' + ', '.join(map(str, values)) + ')' if values else '')
            names[label] = 'if' in job
    if not names:
        raise ValueError(f'{workflow}: no expected jobs')
    return names


def eligible(run, repo, sha, workflow, prefix):
    return (run['head_repository']['full_name'] == repo and run['head_sha'] == sha
            and number(run['workflow_id']) == workflow and run['event'] == 'push'
            and run['head_branch'].startswith(prefix) and run['status'] == 'completed'
            and run['conclusion'] == 'success')


def verdict(runs, jobs, expected):
    """Each candidate must supply every success in its own latest attempt."""
    failures = []
    for run in runs:
        key = number(run['id']), number(run['run_attempt'])
        success = set()
        seen = set()
        for job in jobs[key]:
            name = job['name']
            if name in seen or number(job['run_id']) != key[0] or number(job['run_attempt']) != key[1]:
                raise ValueError(f'{key}: duplicate or unrelated job {name}')
            seen.add(name)
            allowed_skip = expected.get(name, False) and job['conclusion'] == 'skipped'
            if (job['conclusion'] == 'success' or allowed_skip) and job['status'] == 'completed':
                success.add(name)
        missing = expected.keys() - success
        if not missing:
            return []
        failures.extend(f'{key}: {name} missing or unsuccessful' for name in sorted(missing))
    return failures or ['no qualifying push run']


def verify(repo, sha, docs=False):
    failures = []
    base = f'repos/{repo}/actions'
    for workflow in (('check.yml',) if docs else ('check.yml', 'build.yml')):
        expected = {'static-check': False} if docs else expected_jobs(sha, workflow)
        ident = number(api(f'{base}/workflows/{workflow}')['id'])
        rows = pages(f'{base}/workflows/{ident}/runs', 'workflow_runs', {'head_sha': sha})
        runs = [r for r in rows if eligible(r, repo, sha, ident, 'wt/' if docs else 'wt/int-')]
        jobs = {}
        for run in runs:
            rid, attempt = number(run['id']), number(run['run_attempt'])
            jobs[rid, attempt] = pages(f'{base}/runs/{rid}/attempts/{attempt}/jobs', 'jobs', {})
        failures.extend(f'{workflow}: {error}' for error in verdict(runs, jobs, expected))
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sha')
    parser.add_argument('--via')
    args = parser.parse_args()
    try:
        for sha in filter(None, (args.sha, args.via)):
            if not re.fullmatch('[0-9a-f]{40}', sha):
                raise ValueError('expected a full commit SHA')
        repo = os.environ.get('GITHUB_REPOSITORY') or command(
            'gh', 'repo', 'view', '--json', 'nameWithOwner', '--jq', '.nameWithOwner').decode().strip()
        failures = verify(repo, args.via or args.sha)
        if args.via:
            paths = command('git', 'diff', '--no-renames', '--name-only', '-z', args.via, args.sha, '--')
            failures.extend(os.fsdecode(p) for p in paths.split(b'\0') if p and not (
                p.startswith((b'docs/', b'.claude/')) or p.endswith(b'.md')))
            failures.extend(verify(repo, args.sha, docs=True))
    except (OSError, ValueError, KeyError, TypeError, AttributeError, subprocess.SubprocessError, yaml.YAMLError) as exc:
        failures = [str(exc)]
    for error in failures:
        print(error, file=sys.stderr)
    return int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())
