#!/usr/bin/env python3
"""Join T4a-4 MEMSYS5 counts with existing guest F9 and resident mem captures.

Read-only, offline: never operates the emulator. Guest values are never
inferred from host backing or from SQLite's memory counters.
"""
import argparse
import json
from pathlib import Path
import re

CASES = {'small16', 'small64', 'small256', 'threshold', 'holes', 'realloc'}


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def resident(log):
    lines = [line for line in log.splitlines() if 'diag: sbrk_used=' in line]
    assert len(lines) == 1, 'one resident mem snapshot required per file'
    result = {key: int(value) for key, value in fields(lines[0]).items()}
    assert set(result) == {'sbrk_used', 'sbrk_peak', 'sbrk_capacity', 'exec_peak', 'exec_capacity'}
    assert 0 <= result['sbrk_used'] <= result['sbrk_peak'] < result['sbrk_capacity']
    assert 0 <= result['exec_peak'] < result['exec_capacity']
    return result


def capture(budget, user, before, after):
    summaries = [line for line in budget.splitlines() if line.startswith('BUDGET ')]
    assert len(summaries) == 1 and fields(summaries[0])['result'] == 'PASS', 'MEMSYS5 result missing'
    switches = [fields(line) for line in budget.splitlines() if line.startswith('SWITCH ')]
    assert {(r['mode'], r['from'], r['to']) for r in switches} == {
        (str(m), a, b) for m in range(3) for a in 'SML' for b in 'SML'}, 'incomplete switch matrix'
    records = [fields(line) for line in user.splitlines() if line.startswith('F9 v=1 ')]
    assert records and all(row.get('ok') == '1' for row in records), 'F9 failure/missing ok'
    assert any(row.get('phase') == 'result' for row in records), 'parent F9 result missing'
    assert {row['case'] for row in records if row['phase'] == 'reclaim'} == CASES, 'parent reclaim missing'
    assert {row['case'] for row in records if row['phase'] == 'exit'} == CASES, 'USER child exit missing'
    for case in CASES:
        required = {'alloc', 'exit', 'reclaim'}
        if case.startswith('small'):
            required.update(('free', 'trim'))
        elif case == 'threshold':
            required.add('free')
        else:
            required.update(('efull', 'data'))
            if case == 'realloc':
                required.update(('realloc', 'retained'))
        phases = {row['phase'] for row in records if row.get('case') == case}
        assert required <= phases, f'USER measurements missing: {case}'
        for row in records:
            if row.get('case') == case and row['phase'] not in ('reclaim',):
                assert {'bytes', 'us_hi', 'us_lo'} <= row.keys(), 'USER counters/timing missing'
    summary = fields(summaries[0])
    assert int(summary['bytes']) == 512 * 1024
    assert int(summary['denied']) > 0
    assert int(summary['common_measured']) <= int(summary['reserve'])
    return {
        'memsys5': summary,
        'switches': switches,
        'user_malloc': records,
        'resident_before': resident(before),
        'resident_after': resident(after),
        'note': 'MEMSYS5 host byte counts; USER/resident from guest captures. No host timing verdict.',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('budget', 'user', 'mem-before', 'mem-after'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(capture(args.budget.read_text(), args.user.read_text(),
                             args.mem_before.read_text(), args.mem_after.read_text()),
                     ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
