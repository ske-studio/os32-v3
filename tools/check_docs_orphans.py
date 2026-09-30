#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_docs_orphans.py — どこからも辿れない文書 (孤児) の検出

リンク切れ (`tools/check_docs_links.py`) の裏返し。あちらは「指した先が
無い」を見るが、こちらは「**誰からも指されていない**」を見る。lychee の
守備範囲ではないのでこちらで持つ (Python 標準ライブラリだけで動き、
`lychee` は要らない)。

2026-09-15 の文書棚卸しで最も多く見つかった問題がこれだった。票を書いて
`docs/tasks/` に置いたが `docs/INDEX.md` に載せ忘れる、という取りこぼしが
積もると、入口 (CLAUDE.md → INDEX.md) から辿れない文書が増えていく。
書いてあるのに辿れない情報は、次に読む人 (や AI) にとっては無いのと同じで、
同じ調査が何度もやり直される。

## 何をどう見るか

**(1) docs 配下の孤児** — `docs/INDEX.md` を**唯一の起点**として相対リンクを
推移的に辿り、到達できなかった `docs/**/*.md` を挙げる。起点を INDEX.md
1 つに絞るのは、「索引が全体の入口である」という運用 (INDEX.md 冒頭の
正典表) をそのまま検査にしているから。孤児どうしが互いにリンクし合って
いても、入口から辿れなければ孤児のまま出る。

**(2) TDD 記録の孤児** — `tools/tests/*_tdd.md` は票の根拠なので、
`docs/` の索引から辿れる必要はなく、**どれかの票から参照されていれば
よい**。そこでこちらは起点集合に `docs/tasks/**` の票をすべて含める。
受入完了して `docs/archive/**` へ落ちた票も起点に含める — 票が
アーカイブへ動いただけで根拠の記録が孤児になるのはおかしい
(移し方は `tools/move_docs.py`、運用は `docs/archive/README.md`)。
加えて、票は TDD 記録を Markdown リンクではなく
「記録は tools/tests/s0_tdd.md」のように**素のパスで**書くのが慣例なので、
本文中にパスがそのまま現れていれば参照とみなす。

`docs/hw/` は対象外。gitignore された著作権物のミラーで、環境によって
有ったり無かったりする。

## 意図した例外

索引に載せないと決めた文書は `docs/.orphans-allow` に 1 行 1 パスで書く。
`#` から行末はコメント、空行は無視。**「今ある孤児を全部書いて黙らせる」
ためのものではない** — 例外にはその行のコメントで理由を書くこと。

## 使い方

    python3 tools/check_docs_orphans.py          # make check-docs-orphans と同じ
    python3 tools/check_docs_orphans.py --json    # 機械可読
"""

import argparse
import glob
import json
import os
import re
import sys

PROJ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 唯一の起点。運用上「索引が入口」なので、ここから辿れないものを孤児とする。
ROOT_DOC = "docs/INDEX.md"

ALLOW_FILE = "docs/.orphans-allow"

# 走査から外すディレクトリ (docs 直下からの相対)
SKIP_DIRS = {"hw"}

# Markdown のインライン リンク `[text](target)` / `[text](<target> "title")`。
# 参照定義 `[id]: target` の形は OS32 の文書では使っていないので見ない。
LINK_RE = re.compile(r'\]\(\s*<?([^)\s>]+?)>?\s*(?:"[^"]*")?\)')

# 辿ってよい先。docs 配下のほか、移植準備で入った層の README も経路になる。
FOLLOW_PREFIXES = ("docs/", "arch/", "platform/", "tools/")


def read(rel):
    try:
        with open(os.path.join(PROJ_DIR, rel), "r",
                  encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def link_targets(rel):
    """1 ファイルから出ている相対リンクのうち、.md を指すものを正規化して返す。"""
    text = read(rel)
    base = os.path.dirname(rel)
    out = []
    for m in LINK_RE.finditer(text):
        raw = m.group(1).split("#", 1)[0].strip()
        if not raw:
            continue
        # 外部 URL・絶対パス・アンカーのみ は辿らない
        if "://" in raw or raw.startswith(("/", "#", "mailto:")):
            continue
        if not raw.endswith(".md"):
            continue
        norm = os.path.normpath(os.path.join(base, raw)).replace(os.sep, "/")
        if norm.startswith(".."):
            continue
        if norm.startswith(FOLLOW_PREFIXES) or "/" not in norm:
            out.append(norm)
    return out


def reachable(roots):
    """起点集合から推移的に辿れる .md の集合。"""
    seen = set()
    stack = list(roots)
    while stack:
        cur = stack.pop()
        if cur in seen:
            continue
        seen.add(cur)
        if os.path.isfile(os.path.join(PROJ_DIR, cur)):
            stack.extend(link_targets(cur))
    return seen


def docs_md():
    out = []
    for root, dirs, files in os.walk(os.path.join(PROJ_DIR, "docs")):
        rel_root = os.path.relpath(root, PROJ_DIR).replace(os.sep, "/")
        if rel_root == "docs":
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in files:
            if f.endswith(".md"):
                out.append("%s/%s" % (rel_root, f))
    return sorted(out)


def tickets():
    """票。`docs/archive/` に落ちた票も含む (受入完了しただけで票は票)。"""
    out = []
    for sub in ("tasks", "archive"):
        pat = os.path.join(PROJ_DIR, "docs", sub, "**", "*.md")
        out += [os.path.relpath(p, PROJ_DIR).replace(os.sep, "/")
                for p in glob.glob(pat, recursive=True)]
    return sorted(out)


def tdd_records():
    pat = os.path.join(PROJ_DIR, "tools", "tests", "*_tdd.md")
    return sorted(os.path.relpath(p, PROJ_DIR).replace(os.sep, "/")
                  for p in glob.glob(pat))


def load_allow():
    """docs/.orphans-allow を読む。無ければ空。"""
    path = os.path.join(PROJ_DIR, ALLOW_FILE)
    allow = set()
    if not os.path.isfile(path):
        return allow
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                allow.add(line.replace(os.sep, "/"))
    return allow


def mentioned_paths(files):
    """票の本文を連結したもの。素のパスでの言及を見るために使う。"""
    return "\n".join(read(f) for f in files)


def main():
    ap = argparse.ArgumentParser(description="どこからも辿れない文書の検出")
    ap.add_argument("--json", action="store_true", help="機械可読な JSON を出す")
    args = ap.parse_args()

    if not os.path.isfile(os.path.join(PROJ_DIR, ROOT_DOC)):
        print("Error: 起点 %s がありません" % ROOT_DOC, file=sys.stderr)
        return 1

    allow = load_allow()

    # (1) docs 配下 — 起点は INDEX.md ただ 1 つ
    reach = reachable([ROOT_DOC])
    doc_orphans = [p for p in docs_md()
                   if p not in reach and p not in allow]

    # (2) TDD 記録 — 起点に票を含め、素のパス言及も参照とみなす
    tks = tickets()
    reach_tdd = reachable([ROOT_DOC] + tks)
    blob = mentioned_paths([ROOT_DOC] + tks)
    tdd_orphans = [p for p in tdd_records()
                   if p not in reach_tdd and p not in blob and p not in allow]

    if args.json:
        print(json.dumps({
            "root": ROOT_DOC,
            "allow_file": ALLOW_FILE,
            "allowed": sorted(allow),
            "doc_orphans": doc_orphans,
            "tdd_orphans": tdd_orphans,
        }, ensure_ascii=False, indent=2))
        return 1 if (doc_orphans or tdd_orphans) else 0

    if not doc_orphans and not tdd_orphans:
        print("OK — %s から辿れない文書はありません" % ROOT_DOC)
        return 0

    if doc_orphans:
        print("孤児文書 — %s から辿れない docs/*.md が %d 本:"
              % (ROOT_DOC, len(doc_orphans)))
        for p in doc_orphans:
            print("    %s" % p)
        print("")

    if tdd_orphans:
        print("孤児 TDD 記録 — どの票からも参照されていない "
              "tools/tests/*_tdd.md が %d 本:" % len(tdd_orphans))
        for p in tdd_orphans:
            print("    %s" % p)
        print("")

    print("直し方は 3 つのどれか:")
    print("  * 索引 (docs/INDEX.md) か、辿れる票から**リンクを張る**")
    print("  * 役目を終えた文書なら docs/archive/ へ移す "
          "(移したあとも check-docs-links が通ること)")
    print("  * 索引に載せないと決めたなら %s に理由つきで足す" % ALLOW_FILE)
    return 1


if __name__ == "__main__":
    sys.exit(main())
