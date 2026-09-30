#!/usr/bin/env python3
# ========================================================================
#  gen_build_id.py -- カーネルに埋め込むコミット ID の小さな C を作る
#
#  票: docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md 部品 A-4 (ユーザー指示: ver に
#  Commit を出す)。宣言は include/build_id.h。
#
#    python3 tools/gen_build_id.py -o build/out/build_id.c   # 生成 (変化時だけ書く)
#    python3 tools/gen_build_id.py --print                   # ID だけ表示
#
#  ID の決め方:
#    `git rev-parse --short=7 HEAD`。追跡中のファイルに変更があれば "-dirty"
#    (`git status --porcelain --untracked-files=no` が空でない)。未追跡の
#    ファイルは数えない (ビルドの生成物・docs/hw のリンクで常に dirty に
#    なるのを避ける — `git describe --dirty` と同じ基準)。サブモジュール
#    (apps/ game/) の**中の**変更・生成物も数えない (`--ignore-submodules=dirty`、
#    make external がサブモジュールの中に成果物を置く)。サブモジュールの
#    指すコミットが記録と違うときは数える (本体の組み合わせが違う)。
#    git が無い・リポジトリでない・失敗したら "unknown"。
#
#  **中身が同じなら書かない** (mtime を動かさない)。make はこれを毎回走らせるが、
#  build_id.o を組み直すのはコミット ID (か dirty) が変わったときだけで、
#  他のオブジェクトは一切作り直さない。
# ========================================================================

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# include/build_id.h の BUILD_COMMIT_MAX (NUL を含む) と合わせる
COMMIT_MAX = 24
HASH_MAX = COMMIT_MAX - 1 - len('-dirty')


def git(args, cwd):
    r = subprocess.run(['git'] + args, cwd=cwd, capture_output=True, text=True,
                       timeout=30)
    if r.returncode != 0:
        raise OSError(r.stderr.strip() or 'git {} failed'.format(args[0]))
    return r.stdout


def commit_id(cwd=ROOT):
    try:
        h = git(['rev-parse', '--short=7', 'HEAD'], cwd).strip()
        if not h or any(c not in '0123456789abcdef' for c in h):
            return 'unknown'
        h = h[:HASH_MAX]
        dirty = git(['status', '--porcelain', '--untracked-files=no',
                     '--ignore-submodules=dirty'], cwd).strip()
        return h + ('-dirty' if dirty else '')
    except (OSError, subprocess.SubprocessError):
        return 'unknown'


def render(cid):
    return ('/* 生成物 (tools/gen_build_id.py)。手で編集しない。宣言は include/build_id.h */\n'
            'const char os32_build_commit[] = "{}";\n'.format(cid))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('-o', '--output', help='書き出す C ファイル')
    ap.add_argument('--print', action='store_true', help='ID を表示して終わる')
    ap.add_argument('--repo', default=ROOT, help='git を見るディレクトリ (試験用)')
    args = ap.parse_args()

    cid = commit_id(args.repo)
    if len(cid) >= COMMIT_MAX:
        print('Error: commit id too long: {!r}'.format(cid), file=sys.stderr)
        return 1
    if args.print or not args.output:
        print(cid)
        return 0
    text = render(cid)
    try:
        with open(args.output, encoding='utf-8') as f:
            if f.read() == text:
                return 0
    except OSError:
        pass
    d = os.path.dirname(args.output)
    if d:
        os.makedirs(d, exist_ok=True)
    tmp = args.output + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        f.write(text)
    os.replace(tmp, args.output)
    print('build_id: {}'.format(cid))
    return 0


if __name__ == '__main__':
    sys.exit(main())
