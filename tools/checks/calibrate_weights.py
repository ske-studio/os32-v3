#!/usr/bin/env python3
"""Update medians from downloaded check-times artifacts (successful rows only)."""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import statistics


def measurements(paths):
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
            if int(rc) == 0:
                samples[name[6:]].append(value)
    if not samples:
        raise ValueError('no successful timing samples')
    return dict(sources=sources, seconds={n: statistics.median(v) for n, v in sorted(samples.items())},
                samples={n: len(v) for n, v in sorted(samples.items())})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', nargs='+', type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).with_name('weights.json'))
    args = parser.parse_args()
    paths = [p for source in args.artifacts for p in
             ([source] if source.is_file() else source.rglob('check_times.tsv'))]
    args.output.write_text(json.dumps(measurements(paths), indent=2) + '\n')


if __name__ == '__main__':
    main()
