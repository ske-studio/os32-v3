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
    # Timing/geometry and full pixel readback can come from separate replays.
    images = [pair.get('before_pixels', before), pair.get('after_pixels', after)]
    reference = pair.get('reference')
    for capture in [*images, *([reference] if reference is not None else [])]:
        for key in ('machine', 'operation', 'size', 'format', 'initial', 'inputs'):
            if capture[key] != before[key]:
                raise ValueError('different pixel replay ' + key)
        if len(capture['samples']) != len(baseline):
            raise ValueError('incomplete pixel replay')
    p = sum(s['P'] for s in baseline)
    summary = []
    for (run, rows), capture in zip(((before, baseline), (after, replay)), images):
        times = []
        diff = failures = extra = 0
        for i, (sample, row) in enumerate(zip(run['samples'], rows)):
            image = capture['samples'][i]
            failures += bool(sample['rc'] or image['rc'])
            oracle = reference['samples'][i] if reference is not None else image
            failures += bool(oracle['rc'])
            if len(image['frames']) != len(sample['frames']) or len(oracle['frames']) != len(image['frames']):
                raise ValueError('different flip count in pixel replay')
            times.append(sample['us'])
            for timing, frame, full in zip(sample['frames'], image['frames'], oracle['frames']):
                if timing['present'] != frame['present']:
                    raise ValueError('different present in pixel replay')
                actual = bytes.fromhex(frame['scan'])
                expected = bytes.fromhex(full['scan'] if reference is not None else frame['reference'])
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
    rows = run['groups']
    valid = bool(rows) and run.get('completed_groups') == len(rows)
    for row in rows:
        valid &= (row['rc'] == row['diff'] == row['guard_diff'] == 0
                  and row['count'] == 100 and row['us'] >= 0)
        shape = tuple(row[key] for key in ('direction', 'w', 'h', 'pitch', 'offset'))
        if shape not in observed:
            continue
        route, group = row['route'], row['group']
        if route not in ('cpu', 'm2mf') or group not in range(51):
            raise ValueError('invalid group')
        samples = groups[shape][route]
        if group in samples:
            raise ValueError('duplicate group')
        samples[group] = row['us']
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
        allowed = [(size, shape) for size, shape, win, _ in candidates if valid and win and size > last_loss]
        answer[direction] = dict(threshold=min((s for s, _ in allowed), default=None),
                                 shapes=[shape[1:] for _, shape in allowed], measured=candidates)
    return answer


def retry(run):
    """The packed fault replay only passes after the failed area is resent."""
    pending = None
    failures = recovered = 0
    for sample in run['samples']:
        for frame in sample['frames']:
            if 'failed' in frame:
                failures += 1
                pending = frame['failed']
            elif pending is not None:
                draws = frame['present']
                if union_pixels(draws + [pending], *run['size']) != union_pixels(draws, *run['size']):
                    return dict(passed=False, failures=failures, recovered=recovered)
                scan = bytes.fromhex(frame['scan'])
                if len(scan) != math.prod(run['size']) or scan != bytes.fromhex(frame['reference']):
                    return dict(passed=False, failures=failures, recovered=recovered)
                pending = None
                recovered += 1
    return dict(passed=failures == recovered == 1, failures=failures, recovered=recovered)


def serial_trace(lines, size, pixel_format):
    """Decode a single P0a operation or a G5m sweep from its serial capture."""
    samples, observed, groups = [], defaultdict(int), []
    active = pending = frame = None
    operation = mapping = completed_groups = None
    for line in lines:
        fields = line.strip().split()
        if not fields:
            continue
        if fields[0] == 'GXT1':
            if fields[1] == 'pixels':
                if active is None or frame is None or 'scan' in frame:
                    raise ValueError('pixel snapshot outside a flip')
                for key, encoded in zip(('scan', 'reference'), fields[2:], strict=True):
                    runs = bytes.fromhex(encoded)
                    if len(runs) % 2 or 0 in runs[::2] or sum(runs[::2]) != size[0] * size[1]:
                        raise ValueError('incomplete flip pixels')
                    frame[key] = b''.join(bytes([value]) * count for count, value in
                                          zip(runs[::2], runs[1::2])).hex()
                continue
            if fields[1] == 'overflow':
                raise ValueError('incomplete trace: event buffer overflow')
            if fields[1] == 'begin':
                if active is not None or pending is not None or int(fields[2]) != len(samples):
                    raise ValueError('missing or out-of-order input')
                active, frame = [], None
                continue
            if fields[1] == 'end':
                if active is None or int(fields[2]) != len(samples):
                    raise ValueError('unmatched input end')
                pending, active = active, None
                continue
            kind, _, x, y, w, h, n, detail = map(int, fields[1:])
            if kind in (5, 6):
                observed[('put' if kind == 5 else 'get', w, h, n, detail)] += 1
            elif active is not None:
                rect = [x, y, w, h]
                if kind == 0:
                    frame = dict(draw=[], raw=[], snap=[], merge=[], present=[],
                                 previous=[], writes=[])
                    active.append(frame)
                elif frame is None:
                    raise ValueError('rectangle outside a present')
                elif kind == 1:
                    frame['present'].append(rect)
                    frame['draw'].append(rect)
                elif kind == 8:
                    frame['failed'] = rect
                elif kind == 7:
                    frame['page'] = detail
                elif kind == 4:
                    frame['previous'].append(rect)
                elif kind == 2 and pixel_format == 'INDEX8':
                    frame['writes'].append(dict(rect=rect, bytes=n))
                elif kind == 3 and pixel_format == 'PLANAR4':
                    for plane in range(4):
                        frame['writes'].append(dict(rect=rect, bytes=n, plane=plane, page=detail))
                else:
                    raise ValueError('unexpected rectangle type or pixel format')
        elif fields[0] == 'GOP1' and len(fields) == 7 and fields[2].isdigit():
            if pending is None or int(fields[2]) != len(samples):
                raise ValueError('missing input trace')
            if operation is not None and operation != fields[1]:
                raise ValueError('mixed operations')
            operation = fields[1]
            written = sum(w['bytes'] for f in pending for w in f['writes'])
            if written != int(fields[4]):
                raise ValueError('trace disagrees with present_bytes')
            restore = sum((union_pixels(f['previous'] + f['draw'], *size) -
                           union_pixels(f['draw'], *size)) // 2 for f in pending)
            samples.append(dict(frames=pending, us=0 if fields[3] == 'FAILED' else int(fields[3]),
                                rc=int(fields[3] == 'FAILED' or any('failed' in f for f in pending)),
                                commits=int(fields[5]),
                                hw_ops=int(fields[6]), restore_bytes=restore))
            pending = None
        elif fields[:2] == ['G5M1', 'map']:
            if mapping is not None:
                raise ValueError('multiple benchmark sessions')
            mapping = dict(attribute=fields[2], pitch=int(fields[3]),
                           offset=int(fields[4]), span=int(fields[5]))
        elif fields[:2] == ['G5M1', 'done']:
            if mapping is None or completed_groups is not None or len(fields) != 3:
                raise ValueError('unexpected G5m completion')
            completed_groups = int(fields[2])
        elif fields[:2] == ['G5M1', 'invalid']:
            raise ValueError('G5m preflight transfer failed')
        elif fields[0] == 'G5M1':
            if mapping is None or mapping['attribute'] != 'WC':
                raise ValueError('missing WC mapping')
            if completed_groups is not None:
                raise ValueError('group after G5m completion')
            direction, w, h, pitch, offset, route, index, count, us, rc, diff, guard, sync = fields[1:]
            groups.append(dict(direction=direction, w=int(w), h=int(h), pitch=int(pitch),
                               offset=int(offset), route=route, group=int(index), count=int(count),
                               us=int(us), rc=int(rc), diff=int(diff), guard_diff=int(guard),
                               sync=int(sync)))
    if active is not None or pending is not None:
        raise ValueError('truncated input')
    return dict(size=size, format=pixel_format, operation=operation, samples=samples,
                observed=sorted(observed), observed_counts=[[*key, observed[key]] for key in sorted(observed)],
                groups=groups, mapping=mapping, completed_groups=completed_groups)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=['s1', 'compare', 'g5m', 'decode', 'retry'])
    parser.add_argument('trace', type=argparse.FileType('r'))
    parser.add_argument('--machine', help='machine and video configuration for replay comparison')
    parser.add_argument('--initial', help='identifier of the saved initial state and input script')
    parser.add_argument('--size', type=int, nargs=2, metavar=('WIDTH', 'HEIGHT'))
    parser.add_argument('--format', choices=['PLANAR4', 'INDEX8'])
    args = parser.parse_args()
    if args.stage == 'decode':
        if not args.size or min(args.size) <= 0 or not args.format:
            parser.error('decode requires --size WIDTH HEIGHT and --format')
        run = serial_trace(args.trace, args.size, args.format)
        if args.machine and args.initial:
            run.update(machine=args.machine, initial=args.initial,
                       inputs=[[run['operation'], i] for i in range(len(run['samples']))])
        print(json.dumps(run))
        raise SystemExit
    print(json.dumps({'s1': s1, 'compare': compare, 'g5m': g5m, 'retry': retry}[args.stage](json.load(args.trace)), indent=2))
