#!/usr/bin/env python3
"""Query a copied make tree, preserving expansion order and caller overrides."""
import argparse
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_profile
query = build_profile.query


def check():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / 'build').mkdir()
        (root / 'Makefile').write_text('include build/config.mk\nC_KERNEL = $(FIRST) second.c\n')
        (root / 'build/config.mk').write_text('FIRST = first.c\nCC = declared-cc\n')
        values = query(root)
        assert values['C_KERNEL'] == ['first.c', 'second.c']
        (root / 'build/config.mk').write_text('FIRST = copied.c\nCC = declared-cc\n')
        values = query(root)
        assert values['C_KERNEL'] == ['copied.c', 'second.c']
        assert query(root, ['FIRST=override.c'])['C_KERNEL'][0] == 'override.c'
        assert query(root, config_only=True)['CC'] == ['declared-cc']


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    check()
    if args.mutate:
        try:
            original = query
            cached = {}
            def stale(root, *args, **kwargs):
                key = str(root)
                if key not in cached:
                    cached[key] = original(root, *args, **kwargs)
                return cached[key]
            query = stale
            check()
        except AssertionError:
            print('RED: stale query rejected at runtime')
        else:
            raise SystemExit('mutation survived')
    print('PASS build profile')
