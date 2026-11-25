"""Write P0-4 TSV tables from the saved Actions run, job and timer responses."""
import argparse
from collections import Counter, defaultdict
import csv
from datetime import datetime
import json
from pathlib import Path


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


def inventory(ci, out):
    runs = json.loads((ci / 'runs.json').read_text())
    pushes, workflows, jobs, failures, requests = [], [], [], [], []
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
            requests.append((rid, 'all job stdout + per-check logs (SKIP, mutation outcomes, byte count)',
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
    args = parser.parse_args()
    inventory(args.ci, args.out)
