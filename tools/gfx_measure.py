"""Summarize one machine/operation S1 trace or one Ra266 G5m sweep."""
import argparse
from collections import defaultdict
import json
import math


def percentile(values, percent):
    return sorted(values)[math.ceil(len(values) * percent / 100) - 1]


def union_pixels(rectangles, width, height):
    rows = defaultdict(list)
    for x, y, w, h in rectangles:
        left, right = max(0, x), min(width, x + w)
        if left < right:
            for row in range(max(y, 0), min(y + h, height)):
                rows[row].append((left, right))
    pixels = 0
    for spans in rows.values():
        end = 0
        for left, right in sorted(spans):
            pixels += max(0, right - max(left, end))
            end = max(end, right)
    return pixels


def s1(run):
    """Coordinates are final-surface pixels. Plane writes carry actual bytes."""
    result = []
    for sample in run['samples']:
        transferred = written = 0
        stages = defaultdict(int)
        for frame in sample['frames']:
            for name in ('draw', 'raw', 'snap', 'merge', 'present'):
                stages[name] += union_pixels(frame[name], *run['size'])
            for write in frame['writes']:
                size = write['bytes']
                if size < 0:
                    raise ValueError('negative transfer')
                if run['format'] == 'PLANAR4':
                    if write['plane'] not in range(4) or write['page'] < 0:
                        raise ValueError('invalid plane write')
                    transferred += 2 * size
                elif run['format'] == 'INDEX8':
                    transferred += size
                else:
                    raise ValueError('unsupported format')
                written += size
        result.append(dict(P=stages['draw'], T=transferred, bytes=written, stages=dict(stages)))
    return result


def compare(pair):
    """Compare deterministic baseline/replay inputs and raw INDEX8 scan snapshots."""
    before, after = pair['before'], pair['after']
    for key in ('machine', 'operation', 'size', 'format', 'initial', 'inputs'):
        if before[key] != after[key]:
            raise ValueError('different replay ' + key)
    baseline, replay = s1(before), s1(after)
    if len(baseline) != 50 or len(replay) != 50 or len(before['inputs']) != 50:
        raise ValueError('expected 50 measured inputs after warmup')
    p = sum(s['P'] for s in baseline)
    summary = []
    for run, rows in ((before, baseline), (after, replay)):
        times = []
        diff = failures = extra = 0
        for sample, row in zip(run['samples'], rows):
            failures += bool(sample['rc'])
            times.append(sample['us'])
            for frame in sample['frames']:
                actual = bytes.fromhex(frame['scan'])
                expected = bytes.fromhex(frame['reference'])
                if len(actual) != len(expected) or len(actual) != run['size'][0] * run['size'][1]:
                    raise ValueError('incomplete flip snapshot')
                diff += sum(a != b for a, b in zip(actual, expected))
            necessary = sample['restore_bytes']
            if not 0 <= necessary <= row['bytes']:
                raise ValueError('invalid page restoration bytes')
            extra += row['bytes'] - necessary
        t = sum(s['T'] for s in rows)
        summary.append(dict(P=sum(s['P'] for s in rows), T=t, ratio=t/p if p else None,
                            bytes=sum(s['bytes'] for s in rows), extra=extra, diff=diff,
                            failed=failures, p50=percentile(times, 50), p95=percentile(times, 95)))
    base, new = summary
    target = bool(p and 2 * base['T'] > 3 * p)
    valid = not any(s['diff'] or s['failed'] for s in summary)
    passed = valid
    if not p:
        passed = valid and new['extra'] == 0
    elif target:
        passed = valid and 2 * new['T'] <= 3 * p and new['bytes'] < base['bytes']
    return dict(baseline=base, replay=new, target=target, passed=passed)


def g5m(run):
    """Only measured shapes observed at the public entry may enter the table."""
    groups = defaultdict(lambda: defaultdict(dict))
    observed = {tuple(shape) for shape in run['observed']}
    for row in run['groups']:
        shape = tuple(row[key] for key in ('direction', 'w', 'h', 'pitch', 'offset'))
        if shape not in observed:
            continue
        route, group = row['route'], row['group']
        if route not in ('cpu', 'm2mf') or group not in range(51):
            raise ValueError('invalid group')
        samples = groups[shape][route]
        if group in samples:
            raise ValueError('duplicate group')
        samples[group] = (row['us'] if row['rc'] == row['diff'] == row['guard_diff'] == 0
                          and row['count'] == 100 and row['us'] >= 0 else None)
    answer = {}
    for direction in ('put', 'get'):
        candidates = []
        for shape in sorted(observed):
            if shape[0] != direction:
                continue
            stats = {}
            for route, samples in groups[shape].items():
                if len(samples) == 51 and None not in samples.values():
                    values = [samples[i] for i in range(1, 51)]
                    stats[route] = [percentile(values, p) for p in (50, 95)]
            win = len(stats) == 2 and all(a < b for a, b in zip(stats['m2mf'], stats['cpu']))
            candidates.append((shape[1] * shape[2], shape, win, stats))
        last_loss = max((size for size, _, win, _ in candidates if not win), default=-1)
        allowed = [(size, shape) for size, shape, win, _ in candidates if win and size > last_loss]
        answer[direction] = dict(threshold=min((s for s, _ in allowed), default=None),
                                 shapes=[shape[1:] for _, shape in allowed], measured=candidates)
    return answer


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=['s1', 'compare', 'g5m'])
    parser.add_argument('trace', type=argparse.FileType('r'))
    args = parser.parse_args()
    print(json.dumps({'s1': s1, 'compare': compare, 'g5m': g5m}[args.stage](json.load(args.trace)), indent=2))
