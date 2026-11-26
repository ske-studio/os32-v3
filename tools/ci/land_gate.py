#!/usr/bin/env python3
"""Verify the integration push evidence for a fast-forward to main."""
import argparse
import hashlib
import io
import zipfile
import itertools
import json
import os
import re
import subprocess
import sys
import tomllib
from urllib.parse import urlencode
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.impact import comparison
from tests.infra.ci import exemption, scoped_deferral


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


def medium_digest(base, run, artifacts, boot, artifact_id):
    sha = run['head_sha']
    if boot['machine'] == 'pc98':
        name = f"os32-{run['head_branch'].replace('/', '-')}-{sha[:7]}"
        filenames = {'os32_boot.d88', 'os32_boot.img', 'os32_boot144.img', 'os32_install.iso'}
    elif boot['machine'] == 'powermac' and boot['image'] in ('core', 'link', 'sh', 'link-sh', 'shell', 'neg1'):
        name = f"ppc-{boot['image']}-{sha}"
        filenames = {'boot.iso'}
    else:
        raise ValueError('unrecognized boot machine or image')
    name += f"-a{run['run_attempt']}"
    matches = [row for row in artifacts if row['name'] == name and not row['expired']
               and row['id'] == artifact_id]
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


def verify_boot(base, run, artifacts, boot):
    if boot['machine'] == 'pc98':
        count = boot.get('selftest_fail')
        if type(count) is not int or count != 0:
            raise ValueError('PC-98 requires selftest_fail=0')
    rid, attempt = number(boot['run_id']), number(boot['attempt'])
    artifact_id = number(boot['artifact_id'])
    if rid != run['id'] or attempt != run['run_attempt']:
        raise ValueError('boot run or attempt does not match candidate')
    if (medium_digest(base, run, artifacts, boot, artifact_id) != boot['sha256']
            or boot['result'] != 'success' or not boot['reached']
            or not Path(boot['log']).is_file() or digest(boot['media']) != boot['sha256']):
        raise ValueError('invalid boot evidence')


def artifact_json(base, artifacts, name, filename):
    matches = [row for row in artifacts if row["name"] == name and not row["expired"]]
    if len(matches) != 1:
        raise ValueError(f"missing unique selection plan or summary artifact {name}")
    artifact, = matches
    data = command("gh", "api", f"{base}/artifacts/{number(artifact['id'])}/zip")
    if artifact["digest"] != "sha256:" + hashlib.sha256(data).hexdigest():
        raise ValueError(f"{name}: artifact digest mismatch")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        return json.loads(archive.read(filename))


def planned_boots(base, run, artifacts):
    sha, attempt = run["head_sha"], run["run_attempt"]
    plan = artifact_json(base, artifacts, f"plan-{attempt}", "plan.json")
    previous = command("git", "rev-parse", "origin/main").decode().strip()
    command("git", "merge-base", "--is-ancestor", previous, sha)
    identity = dict(sha=sha, kapi_base=previous, run=str(run["id"]), attempt=str(attempt),
                    mutate=True, gate="main")
    rows = tomllib.loads(command("git", "show", f"{sha}:tests/registry.toml").decode())["check"]
    expected = comparison(ROOT, rows, previous, sha, False)
    if (plan["identity"] != identity or plan["base"] != previous or plan["selection"] != expected
            or [[e["id"], e["config"]] for e in plan["entries"]] != expected["entries"]):
        raise ValueError("candidate selection plan differs from current main comparison")
    summary = artifact_json(base, artifacts, f"verified-{sha}-a{attempt}", "summary.json")
    if (type(summary["rc"]) is not int or summary["rc"] != 0 or summary["errors"] != []
            or summary["identity"] != identity or summary["selection"] != expected
            or [[r["id"], r["config"]] for r in summary["rows"]] != expected["entries"]):
        raise ValueError("candidate collection differs from successful expected plan")
    registry = {row["id"]: row for row in rows}
    for result in summary["rows"]:
        row = registry[result["id"]]
        config = result["config"]
        exempt = exemption(row, config, "main")
        applicable = not exempt or scoped_deferral(exempt)
        passed = applicable and result["status"] == "PASS"
        if not passed and (result["status"], result["reason"]) != exempt:
            raise ValueError("unsuccessful selected check")
        mutations = {m["id"] for m in row["mutations"] if applicable and m["config"] == config}
        if (result["mutations"].keys() != mutations
                or any(m["result"] != "RED" for m in result["mutations"].values())):
            raise ValueError("unsuccessful selected mutations")
    machines = set()
    if expected["pc98"] != "none":
        machines.add("pc98")
    if expected["powerpc"]:
        machines.add("powermac")
    return machines


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
    marker = f"verified-{sha}-a{attempt}"
    if sum(row["name"] == marker and not row["expired"] for row in artifacts) != 1:
        raise ValueError("candidate artifact is BUILT, VERIFIED required")
    required = planned_boots(base, run, artifacts)
    machines = set()
    for boot in evidence['boots']:
        machine = boot['machine']
        if boot['sha'] != sha:
            raise ValueError('both boot records must be from the candidate')
        verify_boot(base, run, artifacts, boot)
        machines.add(machine)
    if machines != required:
        return [f"candidate boot records required for {sorted(required)}"]
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
    if not failures:
        print(f"ACCEPTED {args.sha} run={evidence['run_id']} attempt={evidence['attempt']}")
    return int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())
