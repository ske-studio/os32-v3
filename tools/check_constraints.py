#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_constraints.py — プロジェクト制約の参照ずれを検査する

`docs/CONSTRAINTS.md` が規則の正典で、エージェントが読むファイル
(`CLAUDE.md`, `SOUL.md`) には規則行だけを置く、という構成を守らせる。

文言ではなく **ID** (`[C1]`, `[HW2]`, `[ABI3]` …) で照合する。文言は場所に
よって変えてよい (英語/日本語、詳しさの差)。ID が欠けていれば落とす。

検出するずれは 3 種類:

  1. 正典にある ID が参照側に無い    — 新しい規則を足したのに周知されていない
  2. 参照側にある ID が正典に無い    — 規則を消したのに参照が残っている
  3. 正典の ID が重複している        — 採番ミス

`SOUL.md` は未追跡 (エージェントのシステムプロンプト) なので、無ければ
検査対象から外す。あれば検査する。
"""

import os
import re
import sys

PROJ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CANON = os.path.join(PROJ_DIR, "docs", "CONSTRAINTS.md")

# 必須 = 無ければ検査失敗。任意 = 無ければ読み飛ばす。
REFERRERS_REQUIRED = [os.path.join(PROJ_DIR, "CLAUDE.md")]
REFERRERS_OPTIONAL = [os.path.join(PROJ_DIR, "SOUL.md")]

ID_RE = re.compile(r"\[((?:C|HW|ABI|V|D)\d+)\]")
# 正典側の見出し: "### [C1] ..." だけを規則の定義とみなす
HEADING_RE = re.compile(r"^###\s+\[((?:C|HW|ABI|V|D)\d+)\]", re.M)


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def main():
    if not os.path.isfile(CANON):
        print("Error: 正典が見つかりません: %s" % CANON, file=sys.stderr)
        return 1

    canon_text = read(CANON)
    defined = HEADING_RE.findall(canon_text)

    problems = []

    dupes = sorted({i for i in defined if defined.count(i) > 1})
    if dupes:
        problems.append("正典 %s で ID が重複: %s"
                        % (os.path.relpath(CANON, PROJ_DIR), ", ".join(dupes)))

    defined_set = set(defined)
    if not defined_set:
        problems.append("正典に規則の見出し (### [ID] ...) が 1 つも無い")

    checked = []
    for path in REFERRERS_REQUIRED + REFERRERS_OPTIONAL:
        rel = os.path.relpath(path, PROJ_DIR)
        if not os.path.isfile(path):
            if path in REFERRERS_REQUIRED:
                problems.append("参照側が見つかりません: %s" % rel)
            continue
        checked.append(rel)
        found = set(ID_RE.findall(read(path)))

        missing = sorted(defined_set - found, key=lambda s: (s[:-1], int(s[-1])))
        if missing:
            problems.append("%s に載っていない規則: %s" % (rel, ", ".join(missing)))

        stale = sorted(found - defined_set)
        if stale:
            problems.append("%s が正典に無い規則を参照: %s" % (rel, ", ".join(stale)))

    if problems:
        print("=" * 60)
        print("  プロジェクト制約の参照ずれ")
        print("=" * 60)
        for p in problems:
            print("  - %s" % p)
        print("")
        print("  正典: docs/CONSTRAINTS.md")
        print("  規則を足す・消すときは、正典と参照側を同じコミットで直すこと。")
        return 1

    print("制約チェック OK — 規則 %d 件、参照側 %s"
          % (len(defined_set), " / ".join(checked)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
