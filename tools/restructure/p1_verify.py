"""Verify P1 ledgers against the design baseline (P1.md, 母数)."""
import argparse
import csv
import re
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = 'c6076a6f6'
SCOPE = ROOT / 'docs/tasks/restructure/data/scope.tsv'
HEADER = ['パス', '担当', '種類', '根拠', '外の呼び出し元']
EXCLUDED = ['# 基点の棚卸し用ファイル（母数外）']
KINDS = {
    'P1-1': {'製品', 'ビルドの定義', '公開物', '資産', '運用の道具', '第三者の物'},
    'P1-4': {'実行の基盤', '検査の本体', '試験の支援'},
    '文書': {'文書'},
}


def inventory(base):
    data = subprocess.check_output(['git', 'ls-tree', '-rz', '--name-only', base],
                                   cwd=ROOT).decode()
    paths = set(data.rstrip('\0').split('\0'))
    excluded = {p for p in paths if p.startswith('tools/restructure/')
                or re.fullmatch(r'tools/checks/restructure_p0_\d+\.py', p)}
    return paths - excluded, excluded


def compare(label, actual, expected, issues):
    counts = Counter(actual)
    for path in sorted(expected - counts.keys()):
        issues.append(f'{label} 不足: {path}')
    for path in sorted(counts.keys() - expected):
        issues.append(f'{label} 余分: {path}')
    for path, count in counts.items():
        if count > 1:
            issues.append(f'{label} 重複: {path} ({count})')


def verify_scope(table, base):
    expected, omitted = inventory(base)
    issues = []
    pending = []
    paths = []
    exclusions = []
    footer = False
    with table.open(encoding='utf-8', newline='') as stream:
        rows = csv.reader(stream, delimiter='\t', strict=True)
        if next(rows, None) != HEADER:
            issues.append('見出し不一致')
        for row in rows:
            if row == EXCLUDED and not footer:
                footer = True
                continue
            if footer and len(row) == 1:
                exclusions.append(row[0])
                continue
            if footer or len(row) != len(HEADER):
                issues.append(f'{rows.line_num} 行目: 列数または節が不正')
                continue
            path, owner, kind, reason, caller = row
            paths.append(path)
            if any(not value.strip() for value in row[:4]):
                issues.append(f'{path}: 必須欄が空')
            if kind not in KINDS.get(owner, set()):
                issues.append(f'{path}: 担当・種類が不正 ({owner}, {kind})')
            if path.startswith('tools/') and kind not in KINDS['P1-4'] | {'運用の道具'}:
                issues.append(f'{path}: tools の種類が不正')
            if any(tag in reason for tag in ('仮', '保留', '未確定')):
                pending.append((path, reason))
    if not footer:
        issues.append('母数外の節がない')
    compare('担当表', paths, expected, issues)
    compare('母数外', exclusions, omitted, issues)
    for path, reason in pending:
        print(f'A1\t{path}\t{reason}')
    for issue in issues:
        print(issue)
    if issues:
        return 1
    print(f'OK: 対象 {len(paths)}、母数外 {len(exclusions)}、差 0、重複 0、'
          f'必須欄の空 0、A1 確認 {len(pending)}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default=BASE)
    parser.add_argument('table', nargs='?', type=Path, default=SCOPE)
    args = parser.parse_args()
    return verify_scope(args.table, args.base)


if __name__ == '__main__':
    raise SystemExit(main())
