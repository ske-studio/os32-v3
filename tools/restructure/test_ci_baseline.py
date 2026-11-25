"""Exercise the P0-4 inventory with two refs, a failed timer and missing jobs."""
import argparse
import csv
import json
from pathlib import Path
import tempfile
import types


def check(tool, path):
    start, queued, end = [f'2026-11-25T01:00:{s:02d}Z' for s in (0, 3, 13)]
    run = dict(event='push', headBranch='main', headSha='abc', createdAt=start,
               databaseId=1, name='build-fixture', conclusion='failure')
    runs = [run, dict(run, databaseId=2, name='check', conclusion='success'),
            dict(run, databaseId=3, headBranch='branch', createdAt=end, conclusion=''),
            dict(run, databaseId=4, event='schedule')]
    assert [r['databaseId'] for r in tool.recent_pushes(runs, 1)[0]] == [3]
    (path / 'runs.json').write_text(json.dumps(runs))
    job = dict(id=101, name='checks', created_at=start, started_at=queued,
               completed_at=end, conclusion='failure', steps=[dict(name='make', conclusion='failure')])
    for rid, result in [(1, 'failure'), (2, 'success')]:
        (path / f'jobs-{rid}.json').write_text(json.dumps(dict(
            total_count=1, jobs=[dict(job, conclusion=result)])))
    artifact = path / 'times/1/check-times-shard'
    artifact.mkdir(parents=True)
    (artifact / 'check_times.tsv').write_text('check-good\t1\t0\ncheck-bad\t2\t2\n')
    tool.inventory(path, path)

    def rows(name):
        with (path / (name + '.tsv')).open() as stream:
            return list(csv.DictReader(stream, delimiter='\t'))

    assert [r['result'] for r in rows('pushes')] == ['pending', 'failure']
    assert rows('pushes')[1]['elapsed_s'] == '13'
    assert rows('jobs')[0]['queue_s'] == '3' and rows('jobs')[0]['duration_s'] == '10'
    assert rows('runs')[0]['jobs'] == ''
    assert [r['check'] for r in rows('failed_checks')] == ['check-bad']
    assert [r['success_pct_decided'] for r in rows('job_rates')] == ['0.0', '100.0']


def exercise(source):
    tool = types.ModuleType('candidate')
    exec(compile(source, 'ci_baseline.py', 'exec'), tool.__dict__)
    with tempfile.TemporaryDirectory() as directory:
        check(tool, Path(directory))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    source = Path(__file__).with_name('ci_baseline.py').read_text()
    exercise(source)
    if parser.parse_args().mutate:
        mutations = [('reverse=True', 'reverse=False'),
                     ("if 'failure' in results:", 'if False:'),
                     ('if int(rc):', 'if not int(rc):'),
                     ("seconds(job['created_at'], start)", "seconds(start, end)")]
        for old, new in mutations:
            assert source.count(old) == 1
            try:
                exercise(source.replace(old, new))
            except AssertionError:
                print('runtime RED:', old)
            else:
                raise AssertionError('surviving mutation: ' + old)
    print('PASS P0-4 CI inventory')
