"""Compare the observed check results from one pair of probe jobs."""
from collections import Counter
from pathlib import Path
import re
import sys


def mutations(log):
    # A summary can follow individual mutant diagnostics. Count it only once.
    totals = re.findall(r'\b(\d+)(?:/\d+)?\s+runtime RED\b|\bMUTATIONS?\s+(\d+)/\d+ RED\b', log)
    if totals:
        return sum(int(a or b) for a, b in totals)
    lines = re.findall(r'^.*\bMUTATION\b[^\n]*?\bRED\b', log, re.MULTILINE)
    return len(lines) if lines else None


def results(folder):
    expected = set((folder / 'expected').read_text().splitlines())
    rows = {}
    for row in (folder / 'check_times.tsv').read_text().splitlines():
        name, seconds, status = row.split('\t')
        if name in rows:
            raise ValueError(f'duplicate result: {name}')
        log = (folder / 'logs' / (name + '.log')).read_text(errors='replace')
        skips = Counter(line.strip() for line in log.splitlines() if re.search(r'\bSKIP\b', line))
        rows[name] = int(status), mutations(log), skips
    if not expected or rows.keys() != expected:
        raise ValueError(f'{folder}: registration mismatch {expected ^ rows.keys()}')
    return rows


def compare(standard, selfhost):
    if (standard / 'identity').read_bytes() != (selfhost / 'identity').read_bytes():
        raise ValueError('SHA or shard differs')
    left, right = results(standard), results(selfhost)
    if left.keys() != right.keys():
        raise ValueError(f'check names differ: {left.keys() ^ right.keys()}')
    errors = []
    for name in sorted(left):
        rc, count, skips = left[name]
        other_rc, other_count, other_skips = right[name]
        if count is None and other_count is None:
            print(f'{name}: 件数の比較の対象外')
        if rc != other_rc or count != other_count or other_skips - skips:
            errors.append(f'{name}: rc {rc}/{other_rc}, mutations {count}/{other_count}, '
                          f'added SKIP {dict(other_skips - skips)}')
    if errors:
        raise ValueError('\n'.join(errors))
    print(f'{len(left)} checks matched')


if __name__ == '__main__':
    try:
        compare(*(Path(arg) for arg in sys.argv[1:]))
    except (ValueError, OSError) as error:
        sys.exit(str(error))
