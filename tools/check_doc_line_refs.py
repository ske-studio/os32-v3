#!/usr/bin/env python3
"""文書の中の「このリポジトリのソースへの行番号つきの参照」を数え、基準と照らす。

決まり (docs/POLICY_DEV.md §9): リポジトリの文書は、このリポジトリのソースを `パス:行番号` で
指さない (ソースが動くと黙ってずれる)。パスと関数・マクロ・節の名前で指す。版を固定した外部の
手本の file:line は対象外。

数える物: 追跡されている .md (docs/archive/ と docs/hw/ を除く) の中の `<パス>:<行>` で、<パス> が
ディレクトリ付きで、このリポジトリに追跡されているファイルと一致する物。直前が `O/`・`S/` の
ような 1 文字の木の記法でも、パスが一致すれば数える。ファイル名だけの形 (`kernel.c:521`) は、
外部の同名のファイルと区別できないので数えない。

基準 tools/doc_line_refs_baseline.json は {文書: 件数}。文書ごとに、基準より多くても少なくても
落ちる (少ない時は --write-baseline で基準を書き直す)。基準に無い文書は 0 件。
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASELINE = 'tools/doc_line_refs_baseline.json'
EXCLUDED = ('docs/archive/', 'docs/hw/')
# ディレクトリ付きのパス + ':' + 行番号 (範囲・列挙の先頭の数字だけ見る)。
REFERENCE = re.compile(r'((?:[A-Za-z0-9_.\-]+/)+[A-Za-z0-9_.\-]+\.[A-Za-z0-9]+):(\d+)')


def tracked(root):
    out = subprocess.check_output(['git', 'ls-files'], cwd=root, text=True)
    return [name for name in out.split('\n') if name]


def strip_tree_prefix(path):
    """`O/kernel/a.c` のような 1 文字の木の記法の頭を落とす。"""
    head, _, tail = path.partition('/')
    return tail if len(head) == 1 and head.isalpha() and tail else path


def count(text, files):
    """1 本の文書の本文から、このリポジトリのソースへの行番号つきの参照の件数。"""
    total = 0
    for match in REFERENCE.finditer(text):
        path = match[1]
        if path in files or strip_tree_prefix(path) in files:
            total += 1
    return total


def scan(root):
    names = tracked(root)
    files = set(names)
    result = {}
    for name in names:
        if not name.endswith('.md') or name.startswith(EXCLUDED):
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
            problems.append('%s: 行番号つきの参照が %d 件 (基準 %d)。パスと関数・節の名前で指す' % (name, have, allowed))
        elif have < allowed:
            problems.append('%s: %d 件に減った (基準 %d)。--write-baseline で基準を書き直す' % (name, have, allowed))
    return problems


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
        print('doc-line-refs: 基準を書いた (%d 文書、%d 件)' % (len(observed), sum(observed.values())))
        return 0
    baseline = json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {}
    problems = compare(observed, baseline)
    for line in problems:
        print(line)
    print('doc-line-refs: %d 文書、%d 件 (基準 %d 件)' % (len(observed), sum(observed.values()), sum(baseline.values())))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
