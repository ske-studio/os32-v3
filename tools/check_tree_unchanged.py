#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_tree_unchanged.py — 試験がソースを書き換えたまま戻さなかったのを捕まえる。

変異試験は**実物のソースを書き換えて戻す**作りなので、戻し損ねると次の試験が
壊れたソースを読む。打ち切りでも同じことが起きる (docs/POLICY_DEBUG.md §4-40)。
しかも `git add -A` すると壊れたコードがそのままコミットに入る。

使い方 (`make check` の中):
    python3 tools/check_tree_unchanged.py --save <印>    段の前に呼ぶ
    python3 tools/check_tree_unchanged.py --verify <印>  段の後に呼ぶ

見るのは**追跡されているファイルの内容**だけ。未コミットの作業があっても、
段の前後で変わっていなければ通る。終了コード: 0 = 変化なし / 1 = 変化あり。
"""
import hashlib
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAMP_DIR = os.path.join(ROOT, "build", "out")


def tracked():
    p = subprocess.run(["git", "-C", ROOT, "ls-files", "-z"],
                       capture_output=True)
    if p.returncode != 0:
        return None
    return [f for f in p.stdout.decode("utf-8", "replace").split("\0") if f]


def digest(files):
    out = {}
    for rel in files:
        path = os.path.join(ROOT, rel)
        try:
            with open(path, "rb") as f:
                out[rel] = hashlib.sha1(f.read()).hexdigest()
        except OSError:
            out[rel] = "-"
    return out


def main(argv):
    if len(argv) != 3 or argv[1] not in ("--save", "--verify"):
        sys.stderr.write(__doc__)
        return 1
    files = tracked()
    if files is None:
        sys.stderr.write("check_tree_unchanged: git が使えないので見送る\n")
        return 0
    stamp = os.path.join(STAMP_DIR, "tree-%s.json" % argv[2])
    cur = digest(files)
    if argv[1] == "--save":
        if not os.path.isdir(STAMP_DIR):
            os.makedirs(STAMP_DIR)
        with open(stamp, "w", encoding="utf-8") as f:
            json.dump(cur, f)
        return 0

    try:
        with open(stamp, encoding="utf-8") as f:
            old = json.load(f)
    except (OSError, ValueError):
        sys.stderr.write("check_tree_unchanged: %s の控えが無い\n" % argv[2])
        return 1
    bad = [r for r in sorted(set(old) | set(cur))
           if old.get(r, "-") != cur.get(r, "-")]
    os.remove(stamp)
    if not bad:
        return 0
    sys.stderr.write(
        "check_tree_unchanged: **試験がソースを書き換えたまま戻していない** "
        "(%d 件)\n" % len(bad))
    for r in bad[:20]:
        sys.stderr.write("  %s\n" % r)
    if len(bad) > 20:
        sys.stderr.write("  ... 他 %d 件\n" % (len(bad) - 20))
    sys.stderr.write(
        "変異試験は実物を書き換えて戻す。戻し損ねると次の試験が壊れた"
        "ソースを読み、`git add -A` が壊れたコードを拾う。\n"
        "`git checkout -- <path>` で戻してから調べること "
        "(docs/POLICY_DEBUG.md §4-40)。\n")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
