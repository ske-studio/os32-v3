#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
move_docs.py — 文書を動かし、リポジトリ中の参照を追従させる

文書を `git mv` しただけでは、その文書を指していた**相対リンクが黙って
壊れる**。壊れたことは `tools/check_docs_links.py` (lychee) が次に回った
ときに初めて分かり、しかも「どこをどう直せばよいか」は人が 1 本ずつ
数え直すことになる。受入完了した票を `docs/archive/` へ落とす棚卸しでは
1 回で数十本動くので、この追従を手でやると必ず取りこぼす。

そこでこの道具が **(a) `git mv`** と **(b) 参照の書き換え** を 1 手で行う。
将来のアーカイブでも同じ手順で使えるよう、動かす一覧は引数か TSV で
外から与える (この道具は一覧を持たない)。標準ライブラリだけで動く。

## 入力

    python3 tools/move_docs.py --map moves.tsv          # 1 行 "移動元<TAB>移動先"
    python3 tools/move_docs.py SRC DST [SRC DST ...]    # 対で並べる
    python3 tools/move_docs.py --into docs/archive/gui_v11 SRC [SRC ...]
    python3 tools/move_docs.py --map moves.tsv --dry-run   # 書き換え一覧だけ

パスはリポジトリルートからの相対。`--into` は移動先ディレクトリを 1 つ
決めて、ファイル名はそのままで落とす (改名したいときは対か TSV で書く)。
`--map -` で標準入力から読む。TSV の `#` から行末はコメント。

## 何を書き換えるか

対象は **リポジトリ内の追跡されている `.md` すべて** —
`docs/**` (`docs/archive/` も)、`CLAUDE.md`、`README.md`、`arch/README.md`、
`platform/README.md`、`tools/tests/*_tdd.md`、`.claude/skills/**`。
`git ls-files` で集めるので、gitignore されている `docs/hw/` は入らない。

`--ext` で拡張子を足すと、**ソースコードも同じ規則で追従させられる**。票の
番号や設計の正典はヘッダやモジュールの先頭コメントに
「仕様: `docs/tasks/…`」と書く慣例があり、文書だけ動かすとそこが古いまま
残る (2026-09-16 のアーカイブで 27 ファイル取り残した)。

    python3 tools/move_docs.py --map moves.tsv --rewrite-only \
        --ext .c,.h,.inc,.asm,.py,.rs,.mk,.toml,.yaml,.json,.sh,.1 \
        --exclude docs/hw,lib/sqlite3,lib/microtar,lib/zlib

`--ext` は既定 `.md`。並べた拡張子**だけ**が対象になるので、`.md` も一緒に
書き換えたいなら `.md` を明示して並べる。`--exclude` はリポジトリ相対の
接頭辞 (ディレクトリまたはファイル) で、取り込んだ第三者のソースを外す。

`.md` 以外では **(2) 素のパス言及だけ**を当てる。Markdown のリンク規則
(1)(3) は当てない — C の `tbl[i](x)` のような書き方に化けて当たる余地を
残さないため。C や Rust のコメント、Python の docstring、`#` コメントは
どれも地の文なので (2) がそのまま効く。**文字列リテラルの中も区別せずに
当たる**ので、ヘルプ文やメッセージに文書パスを埋めている場合は
`--dry-run` の一覧で確かめてから走らせること。

## 移動が済んでいるとき (`--rewrite-only`)

`--rewrite-only` は `git mv` を行わず、一覧の「移動元 → 移動先」に従って
**参照の書き換えだけ**を行う。既に動かしてしまった後で取りこぼした
種類のファイルを追従させるときに使う。移動元が無く移動先があることを
確かめてから当てる (通常の向きの検査と逆)。動いた文書自身の相対リンクの
引き直しは行わない (移動時に済んでいるため)。

書き換えるのは次の 2 つの形だけで、本文には触らない。

**(1) Markdown のリンク** — `](相対パス)` `](相対パス#見出し)` と、
参照定義 `[label]: 相対パス`。リンク先は**参照元の元の位置**から解決して
リポジトリ相対に直し、動いたファイルを指していれば**参照元の新しい位置**
から見た相対パスに書き直す。`#見出し` は保つ。`http:` `mailto:` などの
スキーム付き、`/` 始まり、`#` だけのものは対象外。

**(2) 素のパス言及** — バッククォートの中などに地の文として書かれた
リポジトリルートからのパス (`docs/archive/settings/TASK_S2.md`,
`tools/tests/s2_tdd.md` など)。票と TDD 記録は
「対象票: `docs/tasks/…`」のように**リンクではなく素のパス**で
書き合う慣例があり (`tools/check_docs_orphans.py` の規則 2、
`tools/gen_tests_inventory.py` の規則 4 がこれを読む)、ここが古いままだと
孤児検査と試験一覧の「票」列が外れる。ルート相対なので参照元の位置には
依らない。前後がパス文字 (英数 `/` `.` `-` `_`) でないときだけ当てるので、
`…/TASK_S2.md` のような長いパスの一部を齧ることはない。

**(3) パスそのものを表示文字にしたリンクのラベル** —
`[tasks/v2/M2](archive/kernel_v2/M2_KAPI_TRAMPOLINE.md)` のように、飛び先だけ
直すと**読者に見える方が古いまま**になる書き方。ラベルが空白を含まないパス片
1 つで、その置き場が今回動いた文書の元の置き場なら、飛び先の新しい置き場に
揃える (`tasks/gui/TASK_K3` `tasks/v2/` のように拡張子が無くても当たる)。
ラベルと飛び先の置き場がもともと同じなら触らない。

(1) を先に処理してリンク先を伏せ字に退避し、(2) を当ててから戻し、最後に (3)。
そうしないと `[`docs/…/TASK_S5.md`](../../docs/…/TASK_S5.md)` のように
リンク先が素のパスを含む書き方で二重に当たって壊れる。

## 動いた文書自身の中のリンク

`docs/tasks/gui/v12/X.md` → `docs/archive/gui_v12/X.md` のように**深さが
変わる**移動では、その文書が持っている `../../08_build.md` のような
リンクがすべてずれる。そこで**動いた文書の中だけは、指す先が動いて
いなくても全部の相対リンクを引き直す** (指し示す先は変えない。
元の位置から解けた同じファイルを、新しい位置から見た形に書き直すだけ)。
動いていない文書では、**動いたファイルを指すリンクだけ**に触る。

## 使い終わったら

    python3 tools/check_docs_links.py        # リンクと見出しアンカー
    python3 tools/check_docs_orphans.py      # 索引から辿れなくなっていないか
    python3 tools/gen_tests_inventory.py --write   # 票の列を引き直す

`docs/INDEX.md` の索引行と、アーカイブ先の README は人が書く。運用は
`docs/archive/README.md`。
"""

import argparse
import os
import posixpath
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

GIT = "git"

# 本文が Markdown として参照を書く 2 つの形。
#   INLINE_RE   [名前](先)  /  [名前](先 "題")  /  [名前](<先>)
#   REFDEF_RE   [label]: 先        (行頭の参照定義)
INLINE_RE = re.compile(r"\]\(\s*<?([^()<>\s]+)>?((?:\s+\"[^\"]*\")?)\s*\)")
REFDEF_RE = re.compile(r"^(\s{0,3}\[[^\]\n]+\]:\s+)(\S+)", re.MULTILINE)

# 書き換えない先。スキーム付き URL、ルート始まり、同一文書内の見出し。
SCHEME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9+.\-]*:")

# 素のパス言及の境界。パスを構成しうる字が隣にあれば当てない。
PATH_CHAR = re.compile(r"[A-Za-z0-9/._\-]")

# リンクの表示文字がパスそのものの `[tasks/v2/M2](…)` の形。ラベル全体が
# パス片 1 つ (空白を含まない) のときだけ当てる。`〜` は `TASK_C1〜C3` 用。
LABEL_RE = re.compile(r"\[(`?)([A-Za-z0-9_./\-〜]*/[A-Za-z0-9_./\-〜]*)\1\]"
                      r"\(([^()<>\s]+)\)")

PLACEHOLDER = "\x00MD%d\x00"


def norm(p):
    """リポジトリ相対の posix パスに正規化する。"""
    p = p.replace(os.sep, "/").strip()
    if posixpath.isabs(p):
        p = posixpath.relpath(p, ROOT.replace(os.sep, "/"))
    return posixpath.normpath(p)


def git(*args):
    return subprocess.check_output([GIT, "-C", ROOT] + list(args),
                                   universal_newlines=True)


def tracked_files(exts, excludes):
    """追跡されているファイルのうち拡張子が exts のもの。

    gitignore された `docs/hw/` は `git ls-files` に出ないので最初から
    入らない。`excludes` はリポジトリ相対の接頭辞。
    """
    out = git("ls-files", "-z")
    files = []
    for f in out.split("\0"):
        if not f:
            continue
        if not any(f.endswith(e) for e in exts):
            continue
        if any(f == x or f.startswith(x.rstrip("/") + "/") for x in excludes):
            continue
        if not os.path.isfile(os.path.join(ROOT, f)):
            continue
        files.append(f)
    return sorted(files)


def read_moves(args):
    """引数 / TSV / --into から「移動元 → 移動先」を作る。"""
    pairs = []
    if args.map:
        if args.map == "-":
            text = sys.stdin.read()
        else:
            with open(args.map, encoding="utf-8") as f:
                text = f.read()
        for lineno, line in enumerate(text.split("\n"), 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            cols = line.split("\t") if "\t" in line else line.split()
            if len(cols) != 2:
                die("--map %s の %d 行目が「移動元<TAB>移動先」でない: %s"
                    % (args.map, lineno, line))
            pairs.append((cols[0], cols[1]))
    if args.into:
        for src in args.paths:
            pairs.append((src, posixpath.join(norm(args.into),
                                              posixpath.basename(norm(src)))))
    elif args.paths:
        if len(args.paths) % 2:
            die("移動元と移動先は対で並べてください (奇数個です)")
        it = iter(args.paths)
        pairs += list(zip(it, it))
    if not pairs:
        die("動かすものがありません (--map / --into / 対の引数 のどれかが要る)")

    moves = {}
    for src, dst in pairs:
        s, d = norm(src), norm(dst)
        if s in moves:
            die("移動元が重複しています: %s" % s)
        if s == d:
            die("移動元と移動先が同じです: %s" % s)
        moves[s] = d
    return moves


def die(msg):
    print("Error: %s" % msg, file=sys.stderr)
    sys.exit(2)


def validate(moves, rewrite_only=False):
    tracked = set(git("ls-files", "-z").split("\0"))
    dsts = {}
    for s, d in sorted(moves.items()):
        if rewrite_only:
            # 移動は済んでいるはず。向きを逆に確かめる。
            if os.path.exists(os.path.join(ROOT, s)):
                die("--rewrite-only なのに移動元がまだあります: %s" % s)
            if not os.path.isfile(os.path.join(ROOT, d)):
                die("移動先がありません: %s" % d)
            if d not in tracked:
                die("移動先が git の追跡下にありません: %s" % d)
        else:
            if not os.path.isfile(os.path.join(ROOT, s)):
                die("移動元がありません: %s" % s)
            if s not in tracked:
                die("移動元が git の追跡下にありません: %s" % s)
            if os.path.exists(os.path.join(ROOT, d)) and d not in moves:
                die("移動先が既にあります: %s" % d)
        if d in dsts:
            die("移動先が衝突しています: %s (%s と %s)" % (d, dsts[d], s))
        dsts[d] = s


def resolve(link, from_dir):
    """参照元ディレクトリから見た相対リンクをリポジトリ相対に直す。

    対象外 (スキーム付き / ルート始まり / 見出しだけ) と、リポジトリの
    外へ出るものは None。
    """
    if not link or link.startswith("#") or link.startswith("/"):
        return None
    if SCHEME_RE.match(link):
        return None
    target = posixpath.normpath(posixpath.join(from_dir, link))
    if target.startswith("../") or target == "..":
        return None
    return target


def rewrite_links(text, moves, old_dir, new_dir, self_moved, hits):
    """(1) Markdown のリンクを書き換え、リンク先を伏せ字に退避する。"""
    saved = []

    def emit(link):
        """書き換え後のリンク文字列。触らないなら None。"""
        path, sep, frag = link.partition("#")
        target = resolve(path, old_dir)
        if target is None:
            return None
        moved_to = moves.get(target)
        if moved_to is None and not self_moved:
            return None            # 動いていない先 × 動いていない参照元
        dest = moved_to if moved_to is not None else target
        new = posixpath.relpath(dest, new_dir) + sep + frag
        if new == link:
            return None            # 引き直しても同じ (同じ籠へ一緒に動いた等)
        return new

    def stash(s):
        saved.append(s)
        return PLACEHOLDER % (len(saved) - 1)

    def on_inline(m):
        new = emit(m.group(1))
        if new is None:
            return "](" + stash(m.group(1)) + m.group(2) + ")"
        hits.append(("link", m.group(1), new, None))
        return "](" + stash(new) + m.group(2) + ")"

    def on_refdef(m):
        new = emit(m.group(2))
        if new is None:
            return m.group(1) + stash(m.group(2))
        hits.append(("refdef", m.group(2), new, None))
        return m.group(1) + stash(new)

    text = INLINE_RE.sub(on_inline, text)
    text = REFDEF_RE.sub(on_refdef, text)
    return text, saved


def rewrite_bare(text, moves, hits):
    """(2) 地の文のルート相対パス言及を書き換える。"""
    # 長いパスから当てる (短いパスが長いパスの接頭辞になる場合の取りこぼし避け)。
    for src in sorted(moves, key=len, reverse=True):
        dst = moves[src]
        start = 0
        while True:
            i = text.find(src, start)
            if i < 0:
                break
            j = i + len(src)
            before_ok = i == 0 or not PATH_CHAR.match(text[i - 1])
            after_ok = j >= len(text) or not PATH_CHAR.match(text[j])
            if before_ok and after_ok:
                hits.append(("bare", src, dst, text.count("\n", 0, i) + 1))
                text = text[:i] + dst + text[j:]
                start = i + len(dst)
            else:
                start = j
    return text


def repair_labels(text, moved_dirs, old_dir, hits):
    """(3) 表示文字がパスそのもののリンクで、その表示文字も追従させる。

    `[tasks/v2/M2](archive/kernel_v2/M2_KAPI_TRAMPOLINE.md)` のように、飛び先だけ
    直すと**読者に見える方は古いまま**になる。ラベルが空白を含まないパス片 1 つで、
    その置き場が今回動いた文書の元の置き場なら、飛び先の新しい置き場に揃える。
    拡張子や節番号の有無は問わない (`tasks/gui/TASK_K3` `tasks/v2/` も当たる)。
    ラベルと飛び先の置き場が既に同じなら触らない。
    """
    def on(m):
        tick, label, target = m.group(1), m.group(2), m.group(3)
        label_dir = posixpath.dirname(label)
        target_dir = posixpath.dirname(target.split("#", 1)[0])
        if not label_dir or label_dir == target_dir:
            return m.group(0)
        if resolve(label_dir + "/", old_dir) not in moved_dirs:
            return m.group(0)
        new = posixpath.join(target_dir, posixpath.basename(label))
        if label.endswith("/") and not new.endswith("/"):
            new += "/"
        hits.append(("label", label, new, None))
        return "[%s%s%s](%s)" % (tick, new, tick, target)

    return LABEL_RE.sub(on, text)


def unstash(text, saved):
    def back(m):
        return saved[int(m.group(1))]
    return re.sub(r"\x00MD(\d+)\x00", back, text)


def plan(moves, exts, excludes, rewrite_only=False):
    """対象を全部舐めて、書き換えが要るものを {path: (new_text, hits)} で返す。

    `.md` は 3 つの規則すべて、それ以外は素のパス言及だけ (docstring の
    「何を書き換えるか」)。
    """
    out = {}
    moved_dirs = set(posixpath.dirname(s) for s in moves)
    for f in tracked_files(exts, excludes):
        path = os.path.join(ROOT, f)
        try:
            with open(path, encoding="utf-8") as fh:
                text = fh.read()
        except UnicodeDecodeError:
            continue           # バイナリ混じりは触らない
        hits = []
        if f.endswith(".md"):
            self_moved = (not rewrite_only) and f in moves
            old_dir = posixpath.dirname(f)
            new_dir = posixpath.dirname(moves.get(f, f))
            body, saved = rewrite_links(text, moves, old_dir, new_dir,
                                        self_moved, hits)
            body = rewrite_bare(body, moves, hits)
            body = unstash(body, saved)
            body = repair_labels(body, moved_dirs, old_dir, hits)
        else:
            body = rewrite_bare(text, moves, hits)
        if body != text:
            out[f] = (body, hits)
    return out


def main():
    ap = argparse.ArgumentParser(
        description="文書を動かし、リポジトリ中の .md の参照を追従させる")
    ap.add_argument("paths", nargs="*",
                    help="移動元と移動先の対 (--into のときは移動元だけ)")
    ap.add_argument("--map", metavar="TSV",
                    help="「移動元<TAB>移動先」の一覧 (`-` で標準入力)")
    ap.add_argument("--into", metavar="DIR",
                    help="移動先ディレクトリ (ファイル名はそのまま)")
    ap.add_argument("--ext", metavar="LIST", default=".md",
                    help="書き換える拡張子をカンマ区切りで (既定 .md)")
    ap.add_argument("--exclude", metavar="LIST", default="",
                    help="外すパスの接頭辞をカンマ区切りで")
    ap.add_argument("--rewrite-only", action="store_true",
                    help="git mv はせず、一覧に従って参照だけ書き換える")
    ap.add_argument("--dry-run", action="store_true",
                    help="動かさず、書き換えの一覧だけ出す")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="書き換えを 1 件ずつ全部出す")
    args = ap.parse_args()

    exts = [e if e.startswith(".") else "." + e
            for e in (x.strip() for x in args.ext.split(",")) if e]
    if not exts:
        die("--ext が空です")
    excludes = [norm(x) for x in
                (y.strip() for y in args.exclude.split(",")) if x]

    moves = read_moves(args)
    validate(moves, args.rewrite_only)
    edits = plan(moves, exts, excludes, args.rewrite_only)

    def count(*kinds):
        return sum(1 for _, (_, h) in edits.items() for k, _, _, _ in h
                   if k in kinds)

    print("移動: %d 本%s" % (len(moves),
                             " (--rewrite-only: 動かさない)"
                             if args.rewrite_only else ""))
    for s in sorted(moves):
        print("  %s -> %s" % (s, moves[s]))
    print("")
    print("対象: %s" % " ".join(exts))
    print("書き換え: %d ファイル / リンク %d 件 / 素のパス %d 件 / 表示文字 %d 件"
          % (len(edits), count("link", "refdef"), count("bare"),
             count("label")))
    for f in sorted(edits):
        _, hits = edits[f]
        print("  %s (%d)" % (f, len(hits)))
        if args.verbose or args.dry_run:
            for kind, old, new, lineno in hits:
                where = "" if lineno is None else ":%d" % lineno
                print("      [%s%s] %s -> %s" % (kind, where, old, new))

    if args.dry_run:
        print("")
        print("--dry-run: 何も書いていません。")
        return 0

    if not args.rewrite_only:
        for s in sorted(moves):
            d = moves[s]
            dstdir = os.path.join(ROOT, posixpath.dirname(d))
            if dstdir and not os.path.isdir(dstdir):
                os.makedirs(dstdir)
            subprocess.check_call([GIT, "-C", ROOT, "mv", s, d])

    for f in sorted(edits):
        body, _ = edits[f]
        dest = os.path.join(ROOT, f if args.rewrite_only else moves.get(f, f))
        with open(dest, "w", encoding="utf-8") as fh:
            fh.write(body)

    print("")
    print("完了。次に回すもの:")
    print("  python3 tools/check_docs_links.py")
    print("  python3 tools/check_docs_orphans.py")
    print("  python3 tools/gen_tests_inventory.py --write")
    return 0


if __name__ == "__main__":
    sys.exit(main())
