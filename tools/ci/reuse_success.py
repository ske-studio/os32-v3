#!/usr/bin/env python3
"""Reuse only successful integration push runs of this workflow and exact SHA."""
import json
import os
import re
import subprocess
import sys
from urllib.parse import urlencode


def api(endpoint):
    return json.loads(subprocess.check_output(['gh', 'api', endpoint], timeout=20,
                                             stderr=subprocess.DEVNULL))


def reusable(env):
    if env.get('GITHUB_EVENT_NAME') != 'push' or env.get('GITHUB_REF') != 'refs/heads/main':
        return False
    sha = env.get('GITHUB_SHA', '')
    repo = env.get('GITHUB_REPOSITORY', '')
    workflow = env.get('CI_WORKFLOW', '')
    if not re.fullmatch(r'[0-9a-f]{40}', sha) or workflow not in ('build.yml', 'check.yml'):
        return False
    identity = api(f'repos/{repo}/actions/workflows/{workflow}')
    workflow_id = identity['id']
    if type(workflow_id) is not int:
        return False
    query = urlencode(dict(head_sha=sha, event='push', status='success', per_page=100))
    runs = api(f'repos/{repo}/actions/workflows/{workflow_id}/runs?{query}')['workflow_runs']
    # A bounded search is sufficient: an older/missing run simply causes execution.
    return any(r['head_sha'] == sha and r['workflow_id'] == workflow_id
               and r['status'] == 'completed' and r['conclusion'] == 'success'
               and r['event'] == 'push' and r['head_branch'].startswith('wt/int-')
               and r['head_repository']['full_name'] == repo
               and str(r['id']) != env.get('GITHUB_RUN_ID') for r in runs)


def main():
    try:
        reuse = reusable(os.environ)
    except (OSError, ValueError, KeyError, TypeError, AttributeError, subprocess.SubprocessError):
        reuse = False
    value = 'true' if reuse else 'false'
    with open(os.environ['GITHUB_OUTPUT'], 'a') as out:
        out.write(f'reuse={value}\n')
    print(f'Prior integration workflow success: {value}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
