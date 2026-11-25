"""Write P0-4 TSV tables from the saved Actions run, job and timer responses."""
import argparse
from collections import Counter, defaultdict
import csv
from datetime import datetime
import json
import re
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ci"))
import time_inventory


def recent_pushes(runs, limit):
    groups = {}
    for run in sorted(runs, key=lambda r: r['createdAt'], reverse=True):
        if run['event'] == 'push':
            key = (run['headBranch'], run['headSha'])
            groups.setdefault(key, []).append(run)
    return list(groups.values())[:limit]


def outcome(results):
    if 'failure' in results:
        return 'failure'
    if any(not r for r in results):
        return 'pending'
    if all(r == 'success' for r in results):
        return 'success'
    return 'cancelled' if 'cancelled' in results else 'other'


def seconds(start, end):
    if start and end:
        delta = (datetime.fromisoformat(end) - datetime.fromisoformat(start)).total_seconds()
        if delta >= 0:
            return int(delta)
    return None


def save(out, name, columns, rows):
    with (out / (name + '.tsv')).open('w', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
        writer.writerow(columns.split())
        writer.writerows(rows)


RED_FORMATS = {
    'RED': rb'RED\b',
    'mutation_prefix': rb'(?:MUTAT(?:E|ION)|mutant|mutation|MUTANT)\b.*?\bRED\b',
    'runtime_prefix': rb'runtime\b.*?\bRED\b',
    'named_reach_marker': rb'[\w.-]+: runtime RED with reach marker$',
}


def measure_logs(directory, out, check_names):
    formats = Counter()
    jobs, attributed = [], []
    sampled = set()
    for manifest in sorted(directory.glob('jobs-*.tsv')):
        rid = manifest.stem.split('-')[1]
        sampled.add(rid)
        for jid, name in csv.reader(manifest.read_text().splitlines(), delimiter='\t'):
            slug = re.sub(r'[^a-zA-Z0-9_-]', '_', name)
            path = directory / f'{rid}-{slug}.log'
            skips = red = ambiguous = named_skips = hidden = 0
            section = ''
            owner = ''
            with path.open('rb') as stream:
                if b'<Code>BlobNotFound</Code>' in stream.readline():
                    jobs.append((rid, jid, name, 'BlobNotFound', None, None, None, None, None, None))
                    continue
                stream.seek(0)
                for raw in stream:
                    line = raw.partition(b'Z ')[2].rstrip(b'\r\n')
                    if line.startswith(b'### '):
                        section = 'skip' if line.startswith('### 通過したが SKIP'.encode()) else ''
                    if section == 'skip':
                        match = re.fullmatch(rb'- `([^`]+)` \((\d+) ' + '行'.encode() + rb'\)', line)
                        if match:
                            count = int(match[2])
                            named_skips += count
                            attributed.append((rid, jid, match[1].decode(), 'gate_skip_summary', count))
                    if line.startswith(b'<details><summary>'):
                        owner = line.removeprefix(b'<details><summary>').partition(' のログの末尾</summary>'.encode())[0].decode()
                    if line == b'</details>':
                        owner = ''
                    if re.match(rb'SKIP\b', line):
                        skips += 1
                        named = re.match(rb'SKIP ([\w.-]+):', line)
                        label = named[1].decode() if named else ''
                        check = label if label in check_names else owner
                        if check:
                            attributed.append((rid, jid, check, 'skip_line', 1))
                    if re.search(rb'\bRED\b', line):
                        # Exclude prose, totals and echoed commands from individual outcomes.
                        form = next((key for key, pattern in RED_FORMATS.items()
                                     if re.match(pattern, line)), None)
                        if form:
                            formats[rid, jid, form] += 1
                            red += 1
                            if owner:
                                attributed.append((rid, jid, owner, 'red_line', 1))
                        else:
                            ambiguous += 1
                    summary = re.fullmatch(rb'\| (\d+) \| (\d+) \| \d+ \| \d+ \| \d+ \| \d+ ' + '秒 '.encode() + rb'\|', line)
                    if summary:
                        hidden += int(summary[2])
            jobs.append((rid, jid, name, 'ok', path.stat().st_size, skips, named_skips, red, ambiguous, hidden))
    save(out, 'log_jobs', 'run_id job_id job status bytes skip_lines gate_reported_skip_lines red_outcome_lines other_red_lines gate_passes_without_stdout', jobs)
    save(out, 'log_checks', 'run_id job_id check evidence count', attributed)
    save(out, 'red_formats', 'run_id job_id format count',
         ((*key, count) for key, count in sorted(formats.items())))
    return sampled


def inventory(ci, out, logs=None, check_names=()):
    sampled = measure_logs(logs, out, check_names) if logs else set()
    runs = json.loads((ci / 'runs.json').read_text())
    pushes, workflows, jobs, failures, requests = [], [], [], [], []
    balance = []
    by_job = defaultdict(list)
    for number, group in enumerate(recent_pushes(runs, 20), 1):
        finishes = []
        created = min(r['createdAt'] for r in group)
        state = outcome([r['conclusion'] for r in group])
        complete = True
        for run in group:
            rid = run['databaseId']
            jobfile = ci / f'jobs-{rid}.json'
            timers = sorted((ci / 'times' / str(rid)).glob('check-times-*/check_times.tsv'))
            if state == 'success' and run['headBranch'].startswith('wt/int-'):
                shards = time_inventory.shard_sums(time_inventory.load(timers))
                for kind, rows in sorted(shards.items()):
                    totals = [total for _, _, total in rows]
                    ratio = time_inventory.imbalance(rows)
                    balance.append((number, rid, kind, len(rows), round(min(totals), 2),
                                    round(max(totals), 2), round(ratio, 2) if ratio else None))
            evidence = []
            for timer in timers:
                for name, _, rc in csv.reader(timer.read_text().splitlines(), delimiter='\t'):
                    if int(rc):
                        failures.append((number, rid, timer.parent.name, name, rc))
                        evidence.append(name)
            data = json.loads(jobfile.read_text()) if jobfile.exists() else {}
            records = data.get('jobs', [])
            if not records or len(records) != data['total_count']:
                complete = False
            for job in records:
                result = job['conclusion']
                name = job['name']
                start, end = job['started_at'], job['completed_at']
                wait = duration = None
                if result != 'skipped':
                    wait = seconds(job['created_at'], start)
                    duration = seconds(start, end)
                if end:
                    finishes.append(end)
                failed_steps = [s['name'] for s in job['steps'] if s['conclusion'] == 'failure']
                jobs.append((number, rid, job['id'], name, result, job['created_at'], start,
                             end, wait, duration, ' | '.join(failed_steps)))
                by_job[(run['name'], name)].append((result, rid))
            workflows.append((number, rid, run['name'], run['createdAt'], run['conclusion'] or 'pending',
                              len(records) if jobfile.exists() else None, len(timers), ','.join(evidence)))
            coverage = ('代表ログあり' if str(rid) in sampled else '1 回分で代表') if sampled else 'job stdout'
            requests.append((rid, coverage,
                             'final jobs JSON' if not records else '',
                             'failed step stdout' if run['conclusion'] == 'failure' else ''))
        elapsed = seconds(created, max(finishes)) if complete and finishes and state != 'pending' else None
        pushes.append((number, created, group[0]['headBranch'], group[0]['headSha'], state, elapsed,
                       ','.join(str(r['databaseId']) for r in group)))
    rates = []
    for (workflow, name), observations in sorted(by_job.items()):
        counts = Counter(r for r, _ in observations)
        decided = counts['success'] + counts['failure']
        rates.append((workflow, name, len(observations), counts['success'], counts['failure'],
                      counts['cancelled'], counts['skipped'],
                      round(100 * counts['success'] / decided, 2) if decided else None,
                      ','.join(str(i) for _, i in observations)))
    save(out, 'shard_balance', 'push run_id group shards min_sum_s max_sum_s ratio', balance)
    save(out, 'pushes', 'push created_utc ref sha result elapsed_s run_ids', pushes)
    save(out, 'runs', 'push run_id workflow created_utc result jobs timers observed_failed_checks', workflows)
    save(out, 'jobs', 'push run_id job_id job result created_utc started_utc ended_utc queue_s duration_s failed_steps', jobs)
    save(out, 'job_rates', 'workflow job observed success failure cancelled skipped success_pct_decided run_ids', rates)
    save(out, 'failed_checks', 'push run_id artifact check rc', failures)
    save(out, 'missing_logs', 'run_id requested_logs requested_metadata failure_detail', requests)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ci', type=Path)
    parser.add_argument('out', type=Path)
    parser.add_argument('--logs', type=Path)
    parser.add_argument('--registry', type=Path, help='saved run.py --list --json for the sampled revision')
    args = parser.parse_args()
    names = {r['name'] for r in json.loads(args.registry.read_text())} if args.registry else set()
    inventory(args.ci, args.out, args.logs, names)
