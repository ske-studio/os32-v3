#!/usr/bin/env python3
"""Update medians from downloaded check-times artifacts (successful rows only).

A check that needs the `qemu-ppc` environment is measured only by the job that provides it
(artifact `check-times-<sha>-ppc-qemu-<i>`). In the x86 shards the same check returns in a
second without running the guest, so those rows are not samples of its cost.
"""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import statistics


QEMU_ENV = 'qemu-ppc'
QEMU_ARTIFACT = '-ppc-qemu-'


def provides_qemu(artifact):
    """True when the artifact comes from the job that boots the PowerPC guest."""
    return QEMU_ARTIFACT in artifact


def measurements(paths, envs=None):
    """envs maps a check name to its declared environments; None keeps every row."""
    samples = defaultdict(list)
    sources = []
    for path in sorted(set(paths)):
        raw = path.read_bytes()
        sources.append(dict(artifact=path.parent.name, sha256=hashlib.sha256(raw).hexdigest()))
        for line in raw.decode().splitlines():
            name, seconds, rc = line.split('\t')
            value = float(seconds)
            if not name.startswith('check-') or not math.isfinite(value) or value < 0:
                raise ValueError(f'invalid timing row: {path}: {line}')
            if envs is not None and (QEMU_ENV in envs.get(name[6:], ())) != provides_qemu(path.parent.name):
                continue
            if int(rc) == 0:
                samples[name[6:]].append(value)
    if not samples:
        raise ValueError('no successful timing samples')
    return dict(sources=sources, seconds={n: statistics.median(v) for n, v in sorted(samples.items())},
                samples={n: len(v) for n, v in sorted(samples.items())})


def merged(stored, fresh):
    """Fresh medians replace the stored ones check by check; unmeasured checks keep theirs."""
    seconds = dict(stored.get('seconds', {}), **fresh['seconds'])
    samples = dict(stored.get('samples', {}), **fresh['samples'])
    return dict(sources=stored.get('sources', []) + fresh['sources'],
                seconds=dict(sorted(seconds.items())), samples=dict(sorted(samples.items())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', nargs='+', type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).with_name('weights.json'))
    parser.add_argument('--merge', action='store_true',
                        help='keep the stored weight of every check these artifacts did not measure')
    args = parser.parse_args()
    paths = [p for source in args.artifacts for p in
             ([source] if source.is_file() else source.rglob('check_times.tsv'))]
    import run
    envs = {name: check.env for name, check in run.discover(Path(__file__).resolve().parents[2]).items()}
    result = measurements(paths, envs)
    if args.merge:
        result = merged(json.loads(args.output.read_text()), result)
    args.output.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
