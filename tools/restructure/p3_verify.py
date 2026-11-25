"""Verify the working tree and a candidate git diff against the P3 ledger."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from move_files import rewrite


def path_only(root, base, old, new, moves):
    original = subprocess.check_output(['git', 'show', f'{base}:{old}'], cwd=root).decode()
    transformed = rewrite(original, old, moves, root)
    lines = []
    for line in transformed.splitlines(keepends=True):
        if line.startswith('INCLUDE_FEATURES += '):
            dirs = []
            for directory in line.split()[2:]:
                dirs.extend(sorted({str(Path(dst).parent) for src, dst in moves.items()
                                    if str(Path(src).parent) == directory}))
            line = 'INCLUDE_FEATURES += ' + ' '.join(dict.fromkeys(dirs)) + '\n'
        lines.append(line)
    return ''.join(lines) == (root / new).read_text()


def verify(root, rows, status, base=None):
    expected = {(row['from'], row['to']) for row in rows}
    actual = set()
    added, deleted = set(), set()
    moves = dict(expected)
    issues = []
    for old, new in sorted(expected):
        if (root / old).exists() or not (root / new).is_file():
            issues.append(f'実体不一致 {old} -> {new}')
    for line in status.splitlines():
        kind, *names = line.split('\t')
        if kind.startswith('R'):
            actual.add(tuple(names))
        elif kind == 'D':
            deleted.update(names)
        elif kind == 'A':
            added.update(names)
    for old, new in sorted(expected - actual):
        if base and old in deleted and new in added and path_only(root, base, old, new, moves):
            print(f'パス置換照合 {old} -> {new}')
            continue
        issues.append(f'改名未検出 {old} -> {new}')
    for old, new in sorted(actual - expected):
        issues.append(f'台帳外改名 {old} -> {new}')
    return issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', help='revision before the move')
    parser.add_argument('--move-status', type=Path, help='git diff -M --name-status at the move boundary')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    rows = json.loads((root / 'docs/tasks/restructure/data/move_map.json').read_text())
    status = subprocess.check_output(
        ['git', 'diff', '-M', '--name-status', args.base], cwd=root, text=True)
    if args.move_status:
        status = args.move_status.read_text()
    issues = verify(root, rows, status, args.base)
    for issue in issues:
        print(issue)
    print(f'移動表 {len(rows)} 件、照合エラー {len(issues)} 件')
    return int(bool(issues))


if __name__ == '__main__':
    raise SystemExit(main())
