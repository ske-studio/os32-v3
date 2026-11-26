#!/usr/bin/env python3
"""Verify the integration push evidence for a fast-forward to main."""
import argparse
import csv
from fnmatch import fnmatchcase
import hashlib
import io
import zipfile
import itertools
import json
import os
import re
import subprocess
import sys
from urllib.parse import urlencode
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from deploy_manifests import CORE_MANIFEST_RELPATHS


def command(*args, timeout=30):
    return subprocess.check_output(args, timeout=timeout)


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


def eligible(run, repo, sha, workflow, prefix):
    return (run['head_repository']['full_name'] == repo and run['head_sha'] == sha
            and number(run['workflow_id']) == workflow and run['event'] == 'push'
            and run['head_branch'].startswith(prefix) and run['status'] == 'completed'
            and run['conclusion'] == 'success')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def medium_digest(base, run, artifacts, boot, artifact_id=None):
    sha = run['head_sha']
    if boot['machine'] == 'pc98':
        name = f"os32-{run['head_branch'].replace('/', '-')}-{sha[:7]}"
        filenames = {'os32_boot.d88', 'os32_boot.img', 'os32_boot144.img', 'os32_install.iso'}
    elif boot['machine'] == 'powermac' and boot['image'] in ('core', 'link', 'sh', 'link-sh', 'shell', 'neg1'):
        name = f"ppc-{boot['image']}-{sha}"
        filenames = {'boot.iso'}
    else:
        raise ValueError('unrecognized boot machine or image')
    matches = [row for row in artifacts if row['name'] == name and not row['expired']
               and (artifact_id is None or row['id'] == artifact_id)]
    if len(matches) != 1 or boot['filename'] not in filenames:
        raise ValueError('boot medium does not identify a unique artifact')
    artifact, = matches
    # Allow one minute per MiB on slow links, with a three-minute minimum.
    timeout = max(180, 60 * (1 + artifact['size_in_bytes'] // (1024 * 1024)))
    archive = command('gh', 'api', f"{base}/artifacts/{artifact['id']}/zip", timeout=timeout)
    if artifact['digest'] != 'sha256:' + hashlib.sha256(archive).hexdigest():
        raise ValueError('downloaded artifact digest mismatch')
    with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
        with bundle.open(boot['filename']) as medium:
            return hashlib.file_digest(medium, 'sha256').hexdigest()


def documentation_only(sha, paths):
    def read(path):
        return command('git', 'show', f'{sha}:{path}').decode('utf-8')

    ledger = read('docs/tasks/restructure/data/scope.tsv')
    docs = {row['パス'] for row in csv.DictReader(io.StringIO(ledger), delimiter='\t')
            if row['所属種類'] == '文書'}
    if not paths <= docs:
        return False
    # P6-0: build/kdebug.mk feeds the HWDEV specification to the generator.
    image_inputs = ['docs/design/hwdev/hwdev_imports.md']
    for manifest in CORE_MANIFEST_RELPATHS:
        entries = yaml.safe_load(read(manifest))['filesystem']['files']
        image_inputs.extend(entry['host'] for entry in entries if not entry.get('host_only'))
    return not any(fnmatchcase(path, pattern) for path in paths for pattern in image_inputs)


def verify_boot(base, repo, workflow, run, artifacts, boot):
    sha = run['head_sha']
    previous = boot['sha']
    rid, attempt = number(boot['run_id']), number(boot['attempt'])
    artifact_id = number(boot['artifact_id'])
    if previous != sha:
        if not re.fullmatch('[0-9a-f]{40}', previous):
            raise ValueError('invalid previous SHA')
        command('git', 'merge-base', '--is-ancestor', previous, 'origin/main')
        command('git', 'merge-base', '--is-ancestor', previous, sha)
        paths = command('git', 'diff', '--no-renames', '--name-only', '-z', previous, sha, '--')
        if not documentation_only(sha, {os.fsdecode(path) for path in paths.split(b'\0') if path}):
            raise ValueError('previous boot record requires a documentation-only change')
        prior = api(f'{base}/runs/{rid}/attempts/{attempt}')
        if (not eligible(prior, repo, previous, workflow, 'wt/') or prior['id'] != rid
                or prior['run_attempt'] != attempt):
            raise ValueError('previous boot run or attempt does not match')
        prior_artifacts = pages(f'{base}/runs/{rid}/artifacts', 'artifacts', {})
        if medium_digest(base, prior, prior_artifacts, boot, artifact_id) != boot['sha256']:
            raise ValueError('previous boot artifact digest mismatch')
        artifact_id = None  # Locate the independently built candidate by name.
    elif rid != run['id'] or attempt != run['run_attempt']:
        raise ValueError('boot run or attempt does not match candidate')
    if (medium_digest(base, run, artifacts, boot, artifact_id) != boot['sha256']
            or boot['result'] != 'success' or not boot['reached']
            or not Path(boot['log']).is_file() or digest(boot['media']) != boot['sha256']):
        raise ValueError('invalid boot evidence')


def verify(repo, sha, evidence):
    workflow = 'restructure-build.yml'
    base = f'repos/{repo}/actions'
    ident = number(api(f'{base}/workflows/{workflow}')['id'])
    rid = number(evidence['run_id'])
    attempt = number(evidence['attempt'])
    run = api(f'{base}/runs/{rid}')
    if (not eligible(run, repo, sha, ident, 'wt/int-') or number(run['id']) != rid
            or number(run['run_attempt']) != attempt
            or evidence['sha'] != sha):
        raise ValueError('candidate SHA or successful latest run attempt does not match')
    artifacts = pages(f'{base}/runs/{rid}/artifacts', 'artifacts', {})
    machines = set()
    for boot in evidence['boots']:
        machine = boot['machine']
        if boot['sha'] != sha:
            raise ValueError('both boot records must be from the candidate')
        verify_boot(base, repo, ident, run, artifacts, boot)
        machines.add(machine)
    if machines != {'pc98', 'powermac'}:
        return ['both PC-98 and PowerMac boot records required']
    return []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sha')
    parser.add_argument('--evidence', required=True, type=Path)
    args = parser.parse_args()
    try:
        if not re.fullmatch('[0-9a-f]{40}', args.sha):
            raise ValueError('expected a full candidate SHA')
        evidence = json.loads(args.evidence.read_text())
        repo = os.environ.get('GITHUB_REPOSITORY') or command(
            'gh', 'repo', 'view', '--json', 'nameWithOwner', '--jq', '.nameWithOwner').decode().strip()
        failures = verify(repo, args.sha, evidence)
    except (OSError, ValueError, KeyError, TypeError, AttributeError, subprocess.SubprocessError, yaml.YAMLError) as exc:
        failures = [str(exc)]
    for error in failures:
        print(error, file=sys.stderr)
    return int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())
