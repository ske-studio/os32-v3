#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_docs_links.py — 文書のリンク切れ検査 (lychee の薄い包み)

`docs/` 以下の Markdown は互いを相対パスで指し合っている。文書を動かしたり
節を改名したりすると、その参照は**黙って**壊れる。壊れていることは読む側が
リンクを踏むまで分からず、AI アシスタントの入口 (CLAUDE.md → INDEX.md →
各仕様) を辿る経路が切れると、そこから先の情報は「無い」のと同じになる。

そこで `lychee` (Rust 製のリンク検査器) に `--offline --include-fragments`
で全 Markdown を舐めさせ、**ファイルの実在**と**見出しアンカーの実在**の
両方を見る。900 リンクを 0.03 秒で見るので `make check` の列に入れてある。

## 何を見るか

  * **相対パスの実在** — `[foo](tasks/gui/DESIGN.md)` の指す先があること。
  * **見出しアンカーの実在** (`--include-fragments`) —
    `[foo](08_build.md#配備3経路)` の `#配備3経路` が実際にその文書の
    見出しから生成されること。日本語の見出しも GitHub と同じ規則で
    正誤を判定できる (登録時に否定試験で確認済み)。
  * 外部 URL は見ない (`--offline`)。ネットワークに依存する検査を
    `make check` に入れると、回線や相手側の都合で落ちるようになる。

## 対象

  * `docs/**/*.md` — ただし `docs/hw/` は除く。あそこは
    `tools/sync_hwdocs.sh` が持ってくる著作権物のミラーで gitignore
    されており、環境によって有ったり無かったりする。
  * `docs/archive/` は**除外しない**。完了した票を archive へ移したあとも
    リンクが生きていることを見るのがこの検査の目的の 1 つで、移動で
    相対パスが 1 段ずれるのはいちばん起きやすい壊し方だから。
  * `CLAUDE.md` / `README.md` — AI と人の入口。
  * `arch/README.md` / `platform/README.md` — 移植準備で入った層の説明。
  * `tools/tests/*_tdd.md` — 票が根拠として指す TDD の記録。

## lychee が無い環境

`SKIP` と出して終了コード 0 (`tools/check_arm_compile.py` と同じ作法)。
`lychee` は cargo で入れる外部の道具で、クロスコンパイラのように
「無ければ OS32 がビルドできない」ものではない。無いことを失敗にすると、
道具を入れていない環境で `make check` が一律に赤くなる。

    cargo install lychee        # 入れ方 (~/.cargo/bin に入る)

`~/.cargo/bin` は PATH に入っていないことが多いので、この包みが自分で
足して探す。

## 使い方

    python3 tools/check_docs_links.py          # make check-docs-links と同じ
    python3 tools/check_docs_links.py -v       # lychee の出力をそのまま全部
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

PROJ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LYCHEE = "lychee"

# PATH に無くても探しに行く場所。cargo install の既定。
EXTRA_PATH = [os.path.expanduser("~/.cargo/bin")]

# lychee に渡す入力。glob は lychee 自身が展開する (シェルではない) ので
# クォートは要らない。展開して 1 つも当たらない入力は渡さない
# (lychee はそれを「読めない入力」として失敗にする)。
INPUTS = [
    "docs/**/*.md",
    "CLAUDE.md",
    "README.md",
    "arch/README.md",
    "platform/README.md",
    "tools/tests/*_tdd.md",
]

# 検査から外すパス。docs/hw は gitignore されたミラー。
# **docs/archive は外さない** — 上の docstring の理由。
EXCLUDE_PATHS = ["docs/hw"]

LYCHEE_FLAGS = ["--offline", "--include-fragments", "--no-progress"]


def find_lychee():
    path = os.environ.get("PATH", "")
    parts = [p for p in path.split(os.pathsep) if p]
    for extra in EXTRA_PATH:
        if extra not in parts:
            parts.append(extra)
    return shutil.which(LYCHEE, path=os.pathsep.join(parts))


def present_inputs():
    """実体のある入力だけを返す。"""
    out = []
    for pat in INPUTS:
        if glob.glob(os.path.join(PROJ_DIR, pat), recursive=True):
            out.append(pat)
    return out


def main():
    ap = argparse.ArgumentParser(description="文書のリンク切れ検査")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="lychee の出力をそのまま全部出す")
    args = ap.parse_args()

    exe = find_lychee()
    if not exe:
        print("SKIP — lychee が無いのでリンク検査を飛ばします "
              "(`cargo install lychee` で入ります)")
        return 0

    inputs = present_inputs()
    if not inputs:
        print("Error: 検査対象の Markdown が 1 つも見つかりません "
              "(実行場所が %s でない可能性)" % PROJ_DIR, file=sys.stderr)
        return 1

    cmd = [exe] + LYCHEE_FLAGS
    for p in EXCLUDE_PATHS:
        cmd += ["--exclude-path", p]
    cmd += ["--"] + inputs

    proc = subprocess.run(cmd, cwd=PROJ_DIR, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, universal_newlines=True)
    out = proc.stdout.rstrip()

    if proc.returncode == 0:
        # 成功時は集計の 1 行だけ (「859 Total ... 0 Errors」)。
        if args.verbose:
            print(out)
        else:
            tail = [l for l in out.split("\n") if l.strip()]
            print("OK — 文書のリンク検査 (lychee): %s"
                  % (tail[-1].strip() if tail else "エラーなし"))
        return 0

    # 失敗時は lychee の列挙をそのまま見せる。どのファイルのどの行が
    # どこを指して外したか、が全部入っている。
    print(out)
    print("")
    print("リンク切れ (または存在しない見出しアンカー) があります。")
    print("  * 文書を動かしたなら、指している側の相対パスを直す")
    print("  * 見出しを変えたなら、`#アンカー` を新しい見出しに合わせる")
    print("  * 再現: python3 tools/check_docs_links.py -v")
    return 1


if __name__ == "__main__":
    sys.exit(main())
