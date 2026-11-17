#!/usr/bin/env python3
"""KAPI は末尾追加だけ ([ABI])。基点の sdk/kapi.json の口の並びが、今の並びの先頭にそのまま在ることを確かめる。

基点は origin/main との merge-base。HEAD がその上に在る (main そのもの) 時は 1 つ前のコミット。
比べるのは ABI を決める欄だけ: 口の名前・戻り値・引数の列、データの欄の名前と型、口の表の容量。
カーネルの側の関数の名前 (`target`)・本体 (`body`)・註は ABI ではないので比べない。

壊すのが正しい変更は、基点から HEAD までのコミットの本文に `KAPI-Break: <理由>` の行を書いて宣言する。
ハッシュで固定する試験を持たない (追加のたびに手で直す値を作らない)。設計は docs/design/tools/check_kapi_abi.md。
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SPEC = 'sdk/kapi.json'


def projection(spec):
    """ABI を決める欄だけを取り出す。"""
    return {
        'api': [(a['name'], a['ret'], tuple(a['args'])) for a in spec['api']],
        'data_fields': [(f['name'], f['type']) for f in spec.get('data_fields', [])],
        'func_capacity': spec.get('func_capacity'),
    }


def compare(base, current):
    """基点と今の食い違いを文の一覧で返す (空なら合格)。"""
    problems = []
    for key, what in (('api', '口'), ('data_fields', 'データの欄')):
        old, new = base[key], current[key]
        if len(new) < len(old):
            problems.append('%sが減った (%d → %d)。末尾追加だけにする' % (what, len(old), len(new)))
        for index, (a, b) in enumerate(zip(old, new)):
            if a != b:
                problems.append('%s %d が変わった: %s → %s' % (what, index, a[0], b[0] if a[0] != b[0] else '同じ名前で型・引数が違う'))
    if base['func_capacity'] != current['func_capacity']:
        problems.append('口の表の容量が変わった (%s → %s)' % (base['func_capacity'], current['func_capacity']))
    return problems


def declared_break(messages):
    """コミットの本文の `KAPI-Break: <理由>` の理由 (無ければ None)。理由の無い宣言は数えない。"""
    for line in messages.split('\n'):
        match = re.fullmatch(r'KAPI-Break:\s*(\S.*)', line.strip())
        if match:
            return match.group(1)
    return None


def git(root, *args):
    result = subprocess.run(['git', *args], cwd=root, capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def base_commit(root):
    head = git(root, 'rev-parse', 'HEAD')
    base = git(root, 'merge-base', 'origin/main', 'HEAD')
    if base is None or head is None:
        return None
    return git(root, 'rev-parse', 'HEAD^') if base == head else base


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--base', help='基点のコミット (既定は origin/main との merge-base)')
    args = parser.parse_args(argv)
    base = args.base or base_commit(args.root)
    old_text = git(args.root, 'show', base + ':' + SPEC) if base else None
    if old_text is None:
        print('kapi-abi: 基点の %s を読めない (履歴の浅い clone か、origin/main が無い)。git fetch origin main' % SPEC)
        return 2
    problems = compare(projection(json.loads(old_text)),
                       projection(json.loads((args.root / SPEC).read_text(encoding='utf-8'))))
    reason = declared_break(git(args.root, 'log', '--format=%B', base + '..HEAD') or '')
    for line in problems:
        print(line)
    if problems and reason:
        print('kapi-abi: 宣言あり (KAPI-Break: %s) — %d 件の食い違いを通す' % (reason, len(problems)))
        return 0
    print('kapi-abi: 基点 %s、食い違い %d 件' % (base[:9], len(problems)))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
