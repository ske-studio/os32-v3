#!/usr/bin/env python3
"""試験の覚え・ソースの註・道具の Python が、アーカイブの文書を指す件数を数え、基準と照らす。

決まり ([D4]、docs/CONSTRAINTS.md): `docs/archive/` の文書は過去の記録で、作業・試験の根拠に
しない。試験の覚え (`tools/tests/*_tdd.md`) やソースの註に残っている票のパスは、票を移した時に
道具が追従させた来歴の記録で、新しく足さない。契約の出所は現行の文書 (`docs/design/`・仕様) を指す。

数える物: 下の 3 つの群のファイルの中の `docs/archive/<…>/<名>.<拡張子>` で、このリポジトリに
追跡されているファイルと一致する物。`docs/archive/README.md` (アーカイブの運用、現行の文書) と、
実在しないパス (試験の作り物) は数えない。

  test-notes  tools/tests/*_tdd.md
  source      .c .h .inc .asm .S
  python      tools/ の下の .py

基準 tools/archive_refs_baseline.json は {ファイル: 件数}。ファイルごとに、基準より多くても
少なくても落ちる (少ない時、票を移した追従で来歴の註が増えた時は --write-baseline で書き直す)。
基準に無いファイルは 0 件。
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASELINE = 'tools/archive_refs_baseline.json'
CURRENT = 'docs/archive/README.md'
SOURCE_EXTS = ('.c', '.h', '.inc', '.asm', '.S')
REFERENCE = re.compile(r'docs/archive/(?:[A-Za-z0-9_\-]+/)*[A-Za-z0-9_.\-]+\.[A-Za-z0-9]+')


def tracked(root):
    out = subprocess.check_output(['git', 'ls-files'], cwd=root, text=True)
    return [name for name in out.split('\n') if name]


def group(name):
    """数える群の名前。対象の外は None。"""
    if name.startswith('tools/tests/') and name.endswith('_tdd.md'):
        return 'test-notes'
    if name.endswith(SOURCE_EXTS):
        return 'source'
    if name.startswith('tools/') and name.endswith('.py'):
        return 'python'
    return None


def count(text, files):
    """1 本のファイルの本文から、実在するアーカイブの文書を指す参照の件数。"""
    total = 0
    for match in REFERENCE.finditer(text):
        path = match[0]
        if path in files and path != CURRENT:
            total += 1
    return total


def scan(root):
    names = tracked(root)
    files = set(names)
    result = {}
    for name in names:
        if group(name) is None:
            continue
        found = count((root / name).read_text(encoding='utf-8', errors='replace'), files)
        if found:
            result[name] = found
    return result


def compare(observed, baseline):
    """基準との食い違いを文の一覧で返す (空なら合格)。"""
    problems = []
    for name in sorted(set(observed) | set(baseline)):
        have, allowed = observed.get(name, 0), baseline.get(name, 0)
        if have > allowed:
            problems.append('%s: アーカイブの文書を指す参照が %d 件 (基準 %d)。契約の出所は現行の文書 '
                            '(docs/design/・仕様) を指す' % (name, have, allowed))
        elif have < allowed:
            problems.append('%s: %d 件に減った (基準 %d)。--write-baseline で基準を書き直す' % (name, have, allowed))
    return problems


def totals(observed):
    out = {'test-notes': 0, 'source': 0, 'python': 0}
    for name, found in observed.items():
        out[group(name)] += found
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--write-baseline', action='store_true')
    args = parser.parse_args(argv)
    observed = scan(args.root)
    path = args.root / BASELINE
    if args.write_baseline:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(observed, indent=1, sort_keys=True, ensure_ascii=False) + '\n', encoding='utf-8')
        print('archive-refs: 基準を書いた (%d ファイル、%d 件)' % (len(observed), sum(observed.values())))
        return 0
    baseline = json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {}
    problems = compare(observed, baseline)
    for line in problems:
        print(line)
    sums = totals(observed)
    print('archive-refs: %d ファイル、%d 件 (試験の覚え %d・ソースの註 %d・道具の Python %d、基準 %d 件)' % (
        len(observed), sum(observed.values()), sums['test-notes'], sums['source'], sums['python'],
        sum(baseline.values())))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
