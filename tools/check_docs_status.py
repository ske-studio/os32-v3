#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_docs_status.py — docs/tasks/ の票の状態行が語彙を守っているかを見る

票の冒頭の `状態:` 行は「その票がいまどこにいるか」の正典 (docs/INDEX.md の
「タスク」節)。ところが語彙を機械で見ていなかったので、2026-09-29 の v2.1 の
棚卸しでは語彙の外の状態行 (実装済み・方針確定・修正済み・レビュー待ち・
追補あり・完了・実機で合格…) が 12 本、実態と食い違うものが 27 本あった。
語彙の外の語は「受入まで済んだのか」が読み手ごとに違って読めるので、
食い違いの温床になる。

## 何を見るか

* 語彙は **docs/POLICY_DEV.md §8 の表** (`<!-- status-vocab:begin -->` と
  `<!-- status-vocab:end -->` の間の 1 列目) から読む。この道具は語を持たない。
  印が無い・表が空なら落ちる (語彙を見失ったまま全部通さない)。
* 対象は `docs/tasks/**/*.md` (シンボリックリンクは辿らない)。`docs/archive/` は
  見ない — 移したあと本文を書き換えない規則 (docs/archive/README.md) があるため。
* 状態行 = 先頭 12 行のうち、`状態:` の直前が行頭・`> `・`/ ` の最初の行。
  `それまでの状態:` のように前に語が付いたものは数えない (古い行を残す書き方)。
* 値の先頭の `**` を外した文字列が、語彙の語のどれかで始まり、その直後が
  区切り (行末・空白・`*`・`(`・`（`・`—`・`。`・`、`) であること。
  長い語から当てる (「受入完了・実機確認待ち」と「受入完了」)。
  「受入完了済み」のように語の後ろに文字が続くものは語彙の外。
* `TASK_*.md`・`HANDOVER_*.md`・`CHECKLIST_*.md`・名前に `PLAN` を含むものは
  状態行が**必須**。それ以外は、状態行があれば語彙を守る。

## 使い方

    python3 tools/check_docs_status.py            # make check-docs-status と同じ
    python3 tools/check_docs_status.py --root DIR # 別の木 (試験用)

試験は tools/tests/test_docs_status.py (記録 tools/tests/docs_status_tdd.md)。
"""

import argparse
import os
import re
import sys

PROJ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POLICY = os.path.join("docs", "POLICY_DEV.md")
TASKS = os.path.join("docs", "tasks")
HEAD_LINES = 12
BEGIN = "<!-- status-vocab:begin -->"
END = "<!-- status-vocab:end -->"
STATUS_RE = re.compile(r"(?:^|> |/ )状態:\s*(.*)$")
BOUNDARY = " \t*(（—。、"
REQUIRED_RE = re.compile(r"^(TASK_|HANDOVER_|CHECKLIST_).*\.md$|PLAN.*\.md$")


def load_vocab(root):
    """POLICY_DEV §8 の表の 1 列目を返す。印か表が無ければ ValueError。"""
    path = os.path.join(root, POLICY)
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if BEGIN not in text or END not in text:
        raise ValueError("%s に語彙の印 (%s / %s) がありません" % (POLICY, BEGIN, END))
    block = text.split(BEGIN, 1)[1].split(END, 1)[0]
    words = []
    for line in block.splitlines():
        line = line.strip()
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        w = cells[0]
        if not w or w == "語" or set(w) <= set("-: "):
            continue
        words.append(w)
    if not words:
        raise ValueError("%s の語彙の表が空です" % POLICY)
    return sorted(words, key=len, reverse=True)


def status_line(path):
    """(行番号, 値) か None。"""
    with open(path, encoding="utf-8") as f:
        for no, line in enumerate(f, 1):
            if no > HEAD_LINES:
                break
            m = STATUS_RE.search(line.rstrip("\n"))
            if m:
                return no, m.group(1)
    return None


def word_of(value, vocab):
    v = value.strip()
    if v.startswith("**"):
        v = v[2:].lstrip()
    for w in vocab:
        if v.startswith(w):
            rest = v[len(w):]
            if not rest or rest[0] in BOUNDARY:
                return w
    return None


def task_docs(root):
    base = os.path.join(root, TASKS)
    out = []
    for d, dirs, files in os.walk(base):
        dirs[:] = sorted(x for x in dirs if not os.path.islink(os.path.join(d, x)))
        for n in sorted(files):
            p = os.path.join(d, n)
            if n.endswith(".md") and not os.path.islink(p):
                out.append(p)
    return out


def check(root):
    """問題の一覧 (文字列) を返す。"""
    vocab = load_vocab(root)
    problems = []
    for p in task_docs(root):
        rel = os.path.relpath(p, root)
        st = status_line(p)
        if st is None:
            if REQUIRED_RE.search(os.path.basename(p)):
                problems.append("%s: 状態行がありません (先頭 %d 行に `状態:`)"
                                % (rel, HEAD_LINES))
            continue
        no, value = st
        if word_of(value, vocab) is None:
            problems.append("%s:%d: 語彙の外の状態行: %s" % (rel, no, value[:60]))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=PROJ_DIR)
    args = ap.parse_args()
    try:
        problems = check(args.root)
    except (OSError, ValueError) as e:
        print("Error: %s" % e, file=sys.stderr)
        return 1
    if problems:
        for s in problems:
            print(s)
        print("NG — 状態行 %d 件。語彙は docs/POLICY_DEV.md §8" % len(problems))
        return 1
    print("OK — docs/tasks/ の状態行はすべて語彙どおり (docs/POLICY_DEV.md §8)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
