#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_select.py — 変更したファイルから「変異込みで回す検査」を選ぶ (make check-changed)。

`make check` は全部の検査を変異込みで回すので約 10 分かかる (2026-09-26 実測)。
変異試験 (否定側) が意味を持つのは**その試験が見ているソースを変えたとき**だけ
なので、変更したファイルに関係する検査だけ変異込みで回し、残りは変異なしで回す。
対応表 (検査 → 入力のパスの glob) は tools/check_map.yaml の 1 か所に置く。

使い方:
    python3 tools/check_select.py --select [--base <ref>]   make check-changed の中
    python3 tools/check_select.py --select --files <パス>...  選び方の試し (git を見ない)
    python3 tools/check_select.py --lint                     make check-map の中
    python3 tools/check_select.py --inputs <検査名>          静的に拾えた入力の一覧
    python3 tools/check_select.py --suggest <検査名>...      対応表の下書き (yaml)

--select の規則 (安全側に倒す):
  * 変更 = `git diff --name-only <base>...HEAD` + 未コミット (staged / unstaged)
    + 追跡外 (gitignore を除く)。`ignore:` に当たるものは数えない。
  * 基点の既定は feat/gui との merge-base。それが HEAD (= feat/gui の上でコミット
    した後) なら HEAD~1
  * 変更が無い                     → 全部を変異なし (= check-fast)
  * `full:` に当たる変更がある     → 全部を変異込み (= check)
    ただし Makefile / build/*.mk (MAKE_INPUT_GLOBS) は **「新しい試験を足す形」だけ**を
    決まった型との完全一致で絞る (make_narrow の docstring、ユーザー決定 2026-10-01 —
    Makefile の変更の安全性を一般に証明するのはやめた。make は呼ばない)。基点
    (merge-base) との差分が「追加だけ」(削除・変更行が 0) で、足した行が全部
    次の型に完全一致するときだけ: (a) CHECK_PAR_TARGETS の列への検査名の追加
    (語の集合で比べる)、(b) 新しい検査の規則 `check-<name>:` + 型どおりの recipe 行、
    (c) 列にある既存の検査の recipe への型どおりの行の追加、(d) コメント行と空行。
    選ぶのは (b) の新しい検査と (c) の行を足した検査。
    それ以外の差分 (削除・変更行、型に合わない行 (`.PHONY:` も)、define / 条件の中、
    継続行の途中、足した非 recipe 行の直後に基点の recipe 行が来る配置、列の移動、
    追加・削除・改名されたファイル、`--files` (基点なし)) は全部 (理由を出す)。
    sdk/kapi.json は生成物を介して試験の中身が変わるので全部のまま。
  * どの検査の glob にも `docs_only:` にも `notest:` にも当たらない変更がある
                                   → 全部を変異込み (= check)。表の漏れで
                                     否定側を落とさないため。`broad:` の検査の
                                     `**` glob はこの判定に数えない
  * 変更が全部 `docs_only:` に当たる → 当たった検査 + `docs_always:` だけを回す
  * 変更が全部 `notest:` (と docs) で、どの検査の glob にも当たらない
                                   → 全部を変異なし (= check-fast)。
                                     どのホスト試験の入力でもない場所の変更
  * それ以外                       → 当たった検査は変異込み、残りは変異なし
                                     (`notest:` の変更は何も足さない)
  検査の glob に当たった変更は `notest:` にも当たっていても「当たった検査」に
  数える (glob が勝つ)。
  出力は sh の代入 (CC_MODE / CC_STAGE1 / CC_MUT1 / CC_SUMMARY)。説明は stderr。
  試験は tools/tests/test_check_select.py (make check-check-select-host)。

--lint が見るもの (make check-map、check-fast / check の列に入っている):
  (a) build/sdk.mk の CHECK_PAR_TARGETS と対応表の検査名が過不足なく一致すること
  (b) 対応表の各 glob (`notest:` も) が追跡されているファイルに 1 つ以上当たること
      (古い glob)
  (c) **漏れ**: 各検査の recipe から辿れる試験スクリプトが開く / #include する
      (取り込む側の場所・ROOT・-I の探索先) / `#[path]` で取り込むソースが、
      その検査の glob に入っていること。
      辿り方は静的 (文字列リテラルと #include "..." だけ) なので、
      os.walk で舐める検査器などは拾えない — そういう検査は glob を手で広く書く。
  (d) **notest の番人**: 各検査の拾えた入力が `notest:` に当たらないこと。
      試験が notest の場所を読むようになったら「notest なのに入力になっている」
      と言って落ちる — notest を狭める (`except:` に足す) か外す。
"""
import os
import re
import shlex
import subprocess
import sys

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP_PATH = os.path.join(ROOT, "tools", "check_map.yaml")


# ---------------------------------------------------------------- 共通
def git(*args):
    p = subprocess.run(["git", "-C", ROOT] + list(args),
                       capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit("check_select: git %s に失敗: %s"
                         % (" ".join(args), p.stderr.strip()))
    return [l for l in p.stdout.splitlines() if l]


_TRACKED = None


def tracked():
    global _TRACKED
    if _TRACKED is None:
        # 未コミットの新しいファイルも入力になりうるので、追跡外 (gitignore を
        # 除く) も数える。
        _TRACKED = set(git("ls-files")) | set(
            git("ls-files", "--others", "--exclude-standard"))
    return _TRACKED


def glob_re(pat):
    """`**` はディレクトリを跨ぐ、`*` `?` は跨がない。`[...]` はそのまま。"""
    out, i = "", 0
    while i < len(pat):
        c = pat[i]
        if pat.startswith("**/", i):
            out += "(?:.*/)?"
            i += 3
        elif pat.startswith("**", i):
            out += ".*"
            i += 2
        elif c == "*":
            out += "[^/]*"
            i += 1
        elif c == "?":
            out += "[^/]"
            i += 1
        elif c == "[":
            j = pat.index("]", i)
            out += pat[i:j + 1]
            i = j + 1
        else:
            out += re.escape(c)
            i += 1
    return re.compile(out + r"\Z")


def compile_globs(globs):
    return [(g, glob_re(g)) for g in globs or []]


def matches(path, cglobs):
    return any(r.match(path) for _, r in cglobs)


def load_map():
    with open(MAP_PATH, encoding="utf-8") as f:
        m = yaml.safe_load(f)
    m.setdefault("ignore", [])
    m.setdefault("full", [])
    m.setdefault("docs_only", [])
    m.setdefault("checks", {})
    m.setdefault("broad", [])
    m.setdefault("docs_always", [])
    m.setdefault("notest", [])
    return m


def compile_notest(entries):
    """notest: の各項目 (glob の文字列、または {glob:, except: [...]}) を
    [(glob, 正規表現, [except の正規表現])] にする。"""
    out = []
    for e in entries or []:
        if isinstance(e, dict):
            g = e["glob"]
            ex = [glob_re(x) for x in e.get("except", []) or []]
        else:
            g, ex = e, []
        out.append((g, glob_re(g), ex))
    return out


def is_notest(path, cnotest):
    return any(r.match(path) and not any(x.match(path) for x in ex)
               for _, r, ex in cnotest)


# ---------------------------------------------------------------- Makefile
# make の入力のうち、基点版との差分を型で絞るもの (`full:` の中の Makefile)。
MAKE_INPUT_GLOBS = ("Makefile", "build/*.mk")


def makefile_paths():
    """ROOT にある MAKE_INPUT_GLOBS のファイル (追跡外も含む)。"""
    out = []
    if os.path.isfile(os.path.join(ROOT, "Makefile")):
        out.append("Makefile")
    bdir = os.path.join(ROOT, "build")
    if os.path.isdir(bdir):
        out += sorted("build/" + f for f in os.listdir(bdir) if f.endswith(".mk"))
    return out


def read_makefiles():
    """{検査名: [recipe 行]} と {変数名: 値} を返す (check-* の規則だけ)。

    --lint / --inputs / --suggest が recipe から試験スクリプトを辿るための
    字面の読みで、make の意味論は持たない (検査列の名前と、タブ行の字面だけ)。
    変異の選び方で build/*.mk の差を判定するのはここではなく make_narrow (型の一致)。"""
    rules, vars_ = {}, {}
    for mf in makefile_paths():
        with open(os.path.join(ROOT, mf), encoding="utf-8") as f:
            lines = f.read().split("\n")
        i, cur = 0, None
        while i < len(lines):
            line = lines[i]
            while line.endswith("\\") and i + 1 < len(lines):
                i += 1
                line = line[:-1] + " " + lines[i].strip()
            m = re.match(r"^([A-Z_][A-Z0-9_]*)\s*:?=\s*(.*)$", line)
            if m:
                vars_[m.group(1)] = m.group(2).split()
                cur = None
            elif re.match(r"^(check[\w-]*)\s*:(?!=)", line):
                cur = re.match(r"^(check[\w-]*)", line).group(1)
                rules.setdefault(cur, [])
            elif line.startswith("\t") and cur:
                rules[cur].append(line.strip())
            elif line.strip() and not line.startswith("#"):
                cur = None
            i += 1
    return rules, vars_


def check_lists(vars_):
    """make check の列 (CHECK_PAR_TARGETS)。2026-09-26 までの逐次の 2 段目
    (CHECK_MUT_TARGETS) は写しの木へ移して消えた — 戻ってきたら止める。"""
    par = vars_.get("CHECK_PAR_TARGETS", [])
    if not par:
        raise SystemExit("check_select: build/sdk.mk に CHECK_PAR_TARGETS が見つからない")
    if vars_.get("CHECK_MUT_TARGETS"):
        raise SystemExit("check_select: CHECK_MUT_TARGETS (逐次の 2 段目) は廃止した — "
                         "変異は写しの木に当てて CHECK_PAR_TARGETS へ "
                         "(docs/archive/tools/TASK_CHECK_MUT_PARALLEL.md §5)")
    return par


# ---------------------------------------------------------------- Makefile の絞り込み (厳格な型)
# Makefile / build/*.mk の変更の安全性を一般に証明するのはやめた (ユーザー決定 2026-10-01。
# 自前の make 解析は独立レビューで 2 回 P1、make -n の比較も 3 回目で P1 — export 変数・
# .ONESHELL・recipe の `-` で展開が同じでも意味が変わる、-j の独立性は順・逆順の一致で
# 保証できない、$(MAKE) / $(shell) の副作用)。絞るのは **「新しい試験を足す形」** だけで、
# 基点 (merge-base) との差分が「追加だけ」で、足した行の 1 本 1 本が下の型に完全一致する
# ときに限る。make は呼ばない (副作用の問題が消える)。
#
# 差分の条件 (全部満たさなければ全部に倒す。理由は stderr):
#   * 変更した make ファイルは基点にも作業中にもある (追加・削除・改名は全部)
#   * 検査の列 (LIST_VAR の `:=` の論理行。基点・作業中とも make ファイル全体で 1 つ、
#     同じファイル、define / 条件の外) の物理行を **1 つの固定マーカー行に畳んだ** 基点の
#     行の列が、同じく畳んだ作業中の行の列の **部分列** になっている = 削除・変更行が 0
#     (git diff の `-` 行 0) で、列の位置も既存行に対して動いていない (同じファイル内の
#     移動も全部 — 独立レビュー P2)。列の物理行は継続 `\` の付け替えで字面が変わるので、
#     **語の集合**で比べる: 基点の語 ⊆ 作業中の語、重複なし、語は全部 NAME_RE。
#     増えた語 = 新しい検査の名前
#   * 作業中にだけある行 (足した行) は、それぞれ 1 行で 1 論理行 (継続行の途中ではない)、
#     define / 条件の外 (make の読み方と、字下げを無視する読み方の **両方**で深さ 0)、
#     そして次のどれかに完全一致:
#       (a)  (列の物理行は上の語集合の比較で見る)
#       (b)  TPL_HEADER_RE  `check-<name>:` — 前提なし。name は列に足した新しい名前で、
#            基点のどの make ファイルにも現れない。規則は 1 つだけ。続く tab 行は
#            次の非 tab 行 (コメント・空行は基点のものも飛ばす) まで**全部足した行**で
#            型どおり、1 行以上 (基点のコメント越しの既存 recipe の横取りを拒む)
#       (c)  TPL_RECIPE_RE  `\tpython3 -B tools/tests/<file>.py [--flag ...] [$(MUT)|$(MUTS)]`
#            — 持ち主 (上へ向かって tab 行・コメント・空行を飛ばした最初の行) が、
#            基点の列にある検査の基点の規則の行 `check-<name>:…` (`=` を含まない) か、
#            (b) の新しい規則。script は作業中の木にあり、対応表の当該検査の glob に当たる
#       (d)  空行 (完全に空) と `#` 始まりのコメント行 (末尾 `\` なし)
#     `.PHONY:` の行は型に入れない (独立レビュー P1: 既存の規則の行と recipe の間に
#     `.PHONY: check-new` を挟むと既存の recipe が .PHONY の所属になる。新しい検査を
#     .PHONY に載せたいなら全部に倒れるのを受け入れる — 載っていない前例はある)
#   * 足した非 recipe 行 (規則・コメント・空行) の **直後 (コメント・空行は基点のものも
#     飛ばす) に基点由来の tab 行が来る** 配置は全部 (既存の規則と recipe の間に挟むと
#     所属が変わる。コメント・空行は make の上では変えないが、同じ配置はまとめて拒否する)
#   * 列に足した名前の集合 = (b) の規則の名前の集合
#   * 作業中の make ファイルに `.ONESHELL` が無い (recipe を 1 つの shell で回すと、
#     足した行で既存の行の終了状態の扱いが変わる)
# 選ぶのは (b) の新しい検査と (c) で行を足した検査。他の変更ファイルの glob の選択と合算。
# 型に入れないもの (全部に倒す): cargo / unittest discover / tools/*.py の検査器の行、
# `$(MUT)` の後ろの旗 (check-fdc-track-host の形)、前提つきの規則、`-B` 無しの python3。
LIST_VAR = "CHECK_PAR_TARGETS"
NAME = r"check-[a-z0-9-]+"
NAME_RE = re.compile(r"^%s$" % NAME)
# 型 (1 か所。docs/08_build.md §8-4 に同じものを書いてある)
TPL_RECIPE_RE = re.compile(
    r"^\tpython3 -B tools/tests/([a-z0-9_]+\.py)((?: --[a-z][a-z-]*)*)( \$\((?:MUT|MUTS)\))?$")
TPL_HEADER_RE = re.compile(r"^(%s):$" % NAME)
TPL_COMMENT_RE = re.compile(r"^#(?:.*[^\\])?$")
# 列の論理行 (継続を結合した後) と、列への代入に見える行 (これが 2 つ以上なら読まない)
LIST_HEAD_RE = re.compile(r"^%s :=(?: (.*))?$" % LIST_VAR)
LIST_ANY_RE = re.compile(r"^\s*(?:override\s+)?%s\s*[:+?!]*=" % LIST_VAR)
# 列の物理行を畳む固定マーカー (NUL を含むので実物の行にはならない)
LIST_MARKER = "\0" + LIST_VAR + "\0"
# (c) の持ち主になれる基点の規則の行
OWNER_RE = re.compile(r"^(%s):(?![:=])" % NAME)
# define / 条件の深さを見る語
DEFINE_OPEN = re.compile(r"^(?:override\s+)?define\b")
DEFINE_CLOSE = re.compile(r"^endef\b")
COND_OPEN = re.compile(r"^(?:ifeq|ifneq|ifdef|ifndef)\b")
COND_CLOSE = re.compile(r"^endif\b")


class Reject(Exception):
    """型に合わない — 全部に倒す理由。"""


def logical_lines(lines):
    """物理行の列を make の論理行 [(start, end, 結合した文字列)] にする。
    末尾が `\\` の行は次の行と結ぶ (make は奇数個の `\\` だけ結ぶが、ここは末尾が `\\` なら
    全部結ぶ = 足した行を「継続行の途中」と見る側に多めに倒す)。"""
    out, i = [], 0
    while i < len(lines):
        s, text = i, lines[i]
        while text.endswith("\\") and i + 1 < len(lines):
            i += 1
            text = text[:-1] + " " + lines[i].strip()
        out.append((s, i, text))
        i += 1
    return out


def depth_flags(llines):
    """各論理行の **手前** で define / 条件の深さが 0 か (= その行は型の判定の対象になれるか)。

    2 通りの読みの両方で 0 のときだけ True: (1) make の読み方 — tab で始まらない行の最初の
    語だけを見て、define の中では条件を数えない。(2) 字下げを無視する読み方 — 先頭の
    空白 / tab を落として全部数える (独立レビューの反例: define の中の endif、タブ付き
    endef、`\\` 継続の次の endef。どちらの読みでも深さ 0 の場所だけ許す)。
    どちらかの読みで深さが負になったら、それ以降は全部 False (構造が読めない)。"""
    flags = []
    d_make = c_make = d_naive = c_naive = 0
    broken = False
    for _, _, text in llines:
        flags.append(not broken and d_make == c_make == d_naive == c_naive == 0)
        if not text.startswith("\t"):
            tok = text.strip()
            if DEFINE_OPEN.match(tok):
                d_make += 1
            elif DEFINE_CLOSE.match(tok) and d_make > 0:
                d_make -= 1
            elif d_make == 0 and COND_OPEN.match(tok):
                c_make += 1
            elif d_make == 0 and COND_CLOSE.match(tok):
                c_make -= 1
        tok = text.strip()
        if DEFINE_OPEN.match(tok):
            d_naive += 1
        elif DEFINE_CLOSE.match(tok):
            d_naive -= 1
        elif COND_OPEN.match(tok):
            c_naive += 1
        elif COND_CLOSE.match(tok):
            c_naive -= 1
        if min(d_make, c_make, d_naive, c_naive) < 0:
            broken = True
    return flags


def find_list(texts):
    """{相対パス: 行の列} から検査の列を探す。(ファイル, 物理行の集合, 語の列) を返す。
    列は make ファイル全体で 1 つだけ、define / 条件の外、語は全部 NAME_RE で重複なし。"""
    found = []
    for rel in sorted(texts):
        ll = logical_lines(texts[rel])
        ok = depth_flags(ll)
        for k, (s, e, text) in enumerate(ll):
            if LIST_ANY_RE.match(text):
                found.append((rel, s, e, text, ok[k]))
    if len(found) != 1:
        raise Reject("%s の代入が %d か所 (1 か所だけ読む)" % (LIST_VAR, len(found)))
    rel, s, e, text, ok = found[0]
    m = LIST_HEAD_RE.match(text)
    if not m or not ok:
        raise Reject("%s の列の形が読めない (%s)" % (LIST_VAR, rel))
    words = (m.group(1) or "").split()
    bad = [w for w in words if not NAME_RE.match(w)]
    if bad:
        raise Reject("%s の列に検査名でない語がある: %s" % (LIST_VAR, bad[0]))
    if len(words) != len(set(words)):
        raise Reject("%s の列に重複がある" % LIST_VAR)
    return rel, set(range(s, e + 1)), words


def collapse_list(lines, lset):
    """列の物理行 (lset) を 1 つのマーカー行に畳む: [(元の行番号 または None, 行)]。
    既存行に対する列の位置はそのまま残る。"""
    out, done = [], False
    for i, l in enumerate(lines):
        if i in lset:
            if not done:
                out.append((None, LIST_MARKER))
            done = True
        else:
            out.append((i, l))
    return out


def inserted_lines(base, work, lset_b, lset_w):
    """列を畳んだ基点の行の列が、畳んだ作業中の行の列の部分列なら、作業中にだけある
    行の番号の列。部分列でない (削除・変更行がある、列の位置が動いた) なら None。"""
    bs, ws = collapse_list(base, lset_b), collapse_list(work, lset_w)
    j, matched = 0, set()
    for _, bl in bs:
        while j < len(ws) and ws[j][1] != bl:
            j += 1
        if j == len(ws):
            return None
        matched.add(j)
        j += 1
    if any(k not in matched and i is None for k, (i, _) in enumerate(ws)):
        return None                      # 作業中の列が基点の列と対応しない
    return [i for k, (i, _) in enumerate(ws) if k not in matched and i is not None]


def _is_skippable(text):
    return text.strip() == "" or text.lstrip().startswith("#")


def classify_file(rel, work, inserted, base_words, new_names, m_checks):
    """1 ファイルの足した行を型に当てる。{検査名: [script]} ((b) は新しい名前、(c) は
    基点の列の名前) と {新しい名前: 規則の数} を返す。合わなければ Reject。
    inserted は inserted_lines() の戻り (列の物理行は入っていない)。"""
    ll = logical_lines(work)
    ok = depth_flags(ll)
    phys2log = {}
    for k, (s, e, _) in enumerate(ll):
        for i in range(s, e + 1):
            phys2log[i] = k
    ins = set(inserted)
    picked, headers = {}, {}

    def single_inserted(k):
        s, e, _ = ll[k]
        return s == e and s in ins

    for i in sorted(ins):
        line = work[i]
        where = "%s:%d" % (rel, i + 1)
        k = phys2log[i]
        if not single_inserted(k):
            raise Reject("%s: 継続行の途中に足している" % where)
        if not ok[k]:
            raise Reject("%s: define / 条件の中に足している" % where)
        if not line.startswith("\t"):
            # 足した非 recipe 行の直後 — コメント・空行 (基点のものも。make は recipe を
            # 切らない) と足した非 tab 行を飛ばした最初の行 — が基点の tab 行なら、既存の
            # 規則と recipe の間に挟んでいる (独立レビュー P1、2 回目は基点のコメント越し)
            j = i + 1
            while j < len(work) and (_is_skippable(work[j]) or (j in ins and not work[j].startswith("\t"))):
                j += 1
            if j < len(work) and j not in ins and work[j].startswith("\t"):
                raise Reject("%s: 足した行の直後に基点の recipe 行が来る (所属が変わる)" % where)
        if line == "" or TPL_COMMENT_RE.match(line):
            continue
        mh = TPL_HEADER_RE.match(line)
        mr = TPL_RECIPE_RE.match(line)
        if mh:
            name = mh.group(1)
            if name not in new_names:
                raise Reject("%s: 規則 %s の名前が列に足した新しい名前でない" % (where, name))
            headers[name] = headers.get(name, 0) + 1
            n = 0
            for k2 in range(k + 1, len(ll)):
                t = ll[k2][2]
                if _is_skippable(t):
                    continue
                if not t.startswith("\t"):
                    break
                # 基点のコメント・空行越しに基点の recipe 行が続いても新しい規則の所属に
                # なる — 配下の tab 行は全部足した行であること (独立レビュー P1 の 2 回目)
                if not single_inserted(k2) or not TPL_RECIPE_RE.match(t):
                    raise Reject("%s: 規則 %s の recipe に基点の行か型に合わない行がある"
                                 % (where, name))
                n += 1
            if n == 0:
                raise Reject("%s: 規則 %s に recipe が無い" % (where, name))
            picked.setdefault(name, [])
        elif mr:
            owner = None
            for k2 in range(k - 1, -1, -1):
                t = ll[k2][2]
                if _is_skippable(t) or t.startswith("\t"):
                    continue
                owner = (k2, t)
                break
            if owner is None:
                raise Reject("%s: recipe 行の持ち主が無い" % where)
            k2, t = owner
            mo = OWNER_RE.match(t)
            if not mo or "=" in t:
                raise Reject("%s: 持ち主が検査の規則の行でない: %r" % (where, t[:60]))
            name = mo.group(1)
            if single_inserted(k2):
                if name not in new_names or not TPL_HEADER_RE.match(t):
                    raise Reject("%s: 持ち主 %s が型に合わない新しい規則" % (where, name))
            elif ll[k2][0] in ins or name not in base_words:
                raise Reject("%s: 持ち主 %s が基点の列にある検査の基点の規則でない" % (where, name))
            script = "tools/tests/" + mr.group(1)
            if not os.path.isfile(os.path.join(ROOT, script)):
                raise Reject("%s: %s が木に無い" % (where, script))
            if not matches(script, m_checks.get(name, [])):
                raise Reject("%s: %s が対応表の %s の glob に入っていない" % (where, script, name))
            picked.setdefault(name, []).append(script)
        else:
            raise Reject("%s: 型に合わない行: %r" % (where, line[:60]))
    return picked, headers


def _base_texts(ancestor):
    """基点のコミットにある MAKE_INPUT_GLOBS のファイル {相対パス: 行の列}。"""
    mk = compile_globs(MAKE_INPUT_GLOBS)
    p = subprocess.run(["git", "-C", ROOT, "ls-tree", "-r", "--name-only", ancestor,
                        "--", "Makefile", "build"], capture_output=True, text=True)
    if p.returncode != 0:
        raise Reject("基点 %s の木を読めない" % ancestor[:12])
    out = {}
    for rel in p.stdout.splitlines():
        if matches(rel, mk):
            q = subprocess.run(["git", "-C", ROOT, "show", "%s:%s" % (ancestor, rel)],
                               capture_output=True)
            if q.returncode != 0:
                raise Reject("基点版 %s を読めない" % rel)
            out[rel] = q.stdout.decode("utf-8", "replace").split("\n")
    return out


def _work_texts():
    out = {}
    for rel in makefile_paths():
        with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
            out[rel] = f.read().split("\n")
    return out


def make_narrow(base, mk_hits, m_checks):
    """Makefile / build/*.mk の変更 (mk_hits) を型の完全一致で絞る。

    (変異込みにする検査の集合 または None, 理由) を返す。None は全部。
    基点は changed_files と同じ merge-base(base, HEAD)。規則は上の節の注釈。"""
    if base is None:
        return None, "基点版なし (--files)"
    p = subprocess.run(["git", "-C", ROOT, "merge-base", base, "HEAD"],
                       capture_output=True, text=True)
    ancestor = p.stdout.strip()
    if p.returncode != 0 or not ancestor:
        return None, "基点 %s と HEAD の merge-base を決められない" % base
    try:
        bt, wt = _base_texts(ancestor), _work_texts()
        for rel in mk_hits:
            if rel not in bt or rel not in wt:
                raise Reject("%s は追加・削除・改名されたファイル" % rel)
        for rel, lines in wt.items():
            if any(".ONESHELL" in l for l in lines):
                raise Reject("%s に .ONESHELL がある" % rel)
        lrel_b, lset_b, bw = find_list(bt)
        lrel_w, lset_w, ww = find_list(wt)
        # 列が別のファイルへ動いた (消して足した) のはマーカーの不一致で「削除・変更行」になる
        base_words, work_words = set(bw), set(ww)
        if not base_words <= work_words:
            raise Reject("%s の列から消えた名前がある: %s"
                         % (LIST_VAR, " ".join(sorted(base_words - work_words))))
        new_names = work_words - base_words
        for name in sorted(new_names):
            rx = re.compile(r"(?<![\w-])%s(?![\w-])" % re.escape(name))
            for rel, lines in bt.items():
                if any(rx.search(l) for l in lines):
                    raise Reject("新しい名前 %s が基点の %s に既にある" % (name, rel))
        picked, headers = {}, {}
        for rel in sorted(mk_hits):
            ins = inserted_lines(bt[rel], wt[rel],
                                 lset_b if rel == lrel_b else set(),
                                 lset_w if rel == lrel_w else set())
            if ins is None:
                raise Reject("%s に削除・変更行がある" % rel)
            pk, hd = classify_file(rel, wt[rel], ins, base_words, new_names, m_checks)
            for t, s in pk.items():
                picked.setdefault(t, []).extend(s)
            for t, n in hd.items():
                headers[t] = headers.get(t, 0) + n
        if set(headers) != new_names or any(n != 1 for n in headers.values()):
            raise Reject("列に足した名前 (%s) と新しい規則 (%s) が一致しない"
                         % (" ".join(sorted(new_names)) or "なし",
                            " ".join(sorted(headers)) or "なし"))
    except Reject as e:
        return None, "型に合わない: %s" % e
    why = "型に一致: 新しい検査 %d 本、recipe に行を足した検査 %d 本" % (
        len(new_names), len(set(picked) - new_names))
    return set(picked), why


# ---------------------------------------------------------------- 入力の抽出
PY_STR = re.compile(r"""(?:[rbfRBF]{0,2})(['"])([^'"\n]{1,200})\1""")
PY_JOIN = re.compile(
    r"""(?:(?:['"][\w.+-][\w./+-]*['"])\s*(?:/|,)\s*)+['"][\w.+-][\w./+-]*['"]""")
C_INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
RS_PATH = re.compile(r'#\[path\s*=\s*"([^"]+)"\]')
PY_IMPORT = re.compile(r"^\s*(?:from\s+([\w.]+)\s+import|import\s+([\w., ]+))",
                       re.M)
TOML_PATH = re.compile(r'path\s*=\s*"([^"]+)"')

SCAN_ROOTS = ("tools/", "userland/gshell/host/")   # ここの下は中身まで辿る
# C の実装は場所を問わず #include "..." を多段に辿る。実装の .c が取り込む
# .inc (コードの断片、hsync.c → hsync_protect.inc など) を表から落とさないため
# (代行レビュー P2-1、2026-09-26)。
C_EXTS = (".c", ".h", ".inc")
# "/" を含まない裸のファイル名でも、この拡張子なら ROOT 相対の候補にする
# (check_kapi_version.py の README.md、check_manifests.py の CLAUDE.md — P2-2)。
BARE_EXTS = (".md", ".yaml", ".yml", ".json", ".tsv")


def norm(p):
    return os.path.normpath(p).replace(os.sep, "/")


def is_tracked_file(rel):
    return rel in tracked()


def tracked_under(d):
    d = d.rstrip("/") + "/"
    return [f for f in tracked() if f.startswith(d)]


class Extractor:
    """1 つの検査の入力を静的に拾う。

    C の `#include "x.h"` は、取り込む側の場所 → ROOT → **-I の探索先** の順に
    探す。探索先はその検査の recipe の `-I<dir>` と、辿った試験スクリプトの
    文字列 (`"-Iinclude"`、`"-I" + str(ROOT / p) for p in ("include", "fs")` の
    "include" "fs" のような、追跡されている .h を持つディレクトリ名) から集める。
    探索先が出そろってから C を読むため、C の走査は後回しの列 (pending) に積む。
    どの探索先で見つかるかは -I の順で決まるが、順は追わずに**当たったもの全部**
    を入力に数える (多めに数える = 変異を回す側に倒れる)。"""

    def __init__(self):
        self.seen = set()
        self.found = set()
        self.inc_dirs = set()
        self.pending = []

    def add(self, rel):
        rel = norm(rel)
        if rel.startswith("../") or not is_tracked_file(rel):
            return
        self.found.add(rel)
        if rel in self.seen:
            return
        self.seen.add(rel)
        if rel.endswith(C_EXTS):
            self.pending.append(rel)
        elif rel.startswith(SCAN_ROOTS) or rel.endswith("/Cargo.toml") \
                or "/host_tests/" in rel:
            self.scan(rel)

    def add_inc_dir(self, d):
        d = norm(d) if d not in ("", ".") else ""
        if d.startswith("../") or os.path.isabs(d):
            return
        if d == "" or d in header_dirs():
            self.inc_dirs.add(d)

    def drain(self):
        """後回しにした C を、集まった -I の探索先で読む。"""
        while self.pending:
            self.scan(self.pending.pop())

    def add_dir(self, rel):
        for f in tracked_under(norm(rel)):
            self.add(f)

    def resolve(self, cand, base_dir):
        """ROOT 相対・ファイルの場所相対の順に、追跡されているものを探す。"""
        for base in ("", base_dir):
            rel = norm(os.path.join(base, cand))
            if is_tracked_file(rel):
                return ("f", rel)
            if tracked_under(rel) and rel not in (".", ""):
                return ("d", rel)
        return None

    def scan(self, rel):
        path = os.path.join(ROOT, rel)
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            return
        d = os.path.dirname(rel)
        if rel.endswith(".py"):
            self.scan_py(text, d)
        elif rel.endswith(C_EXTS):
            # "..." はまず取り込む側の場所から探し (C の規則)、無ければ ROOT と
            # -I の探索先 (self.inc_dirs) で当たったものを全部数える。
            # <...> は追わない (システムのヘッダ)。循環は add() の seen で止まる。
            for inc in C_INC.findall(text):
                own = norm(os.path.join(d, inc))
                if is_tracked_file(own):
                    self.add(own)
                    continue
                for base in sorted(self.inc_dirs | {""}):
                    rel2 = norm(os.path.join(base, inc))
                    if is_tracked_file(rel2):
                        self.add(rel2)
        elif rel.endswith(".rs"):
            for p in RS_PATH.findall(text):
                self.add(os.path.join(d, p))
        elif rel.endswith("Cargo.toml"):
            self.add_crate(d)
            for p in TOML_PATH.findall(text):
                self.add_crate(norm(os.path.join(d, p)))

    def add_crate(self, d):
        if ("crate", d) in self.seen:
            return
        self.seen.add(("crate", d))
        for f in tracked_under(d):
            if f.endswith((".rs", ".toml")):
                self.add(f)

    def scan_py(self, text, d):
        cands = set()
        if re.search(r"""['"]-I""", text):
            # -I の探索先: "-I<dir>" の文字列と、.h を持つディレクトリ名の文字列
            for _, s in PY_STR.findall(text):
                s = s.strip()
                if s.startswith("-I"):
                    self.add_inc_dir(s[2:])
                elif " " not in s and not s.startswith("/"):
                    self.add_inc_dir(s)
        for m in PY_JOIN.finditer(text):
            parts = re.findall(r"""['"]([^'"]+)['"]""", m.group(0))
            cands.add("/".join(parts))
        for _, s in PY_STR.findall(text):
            if " " in s:
                continue
            if "/" in s or s.endswith(BARE_EXTS):
                cands.add(s)
        for c in cands:
            c = c.strip()
            if not c or c.startswith("/") or "*" in c or "$" in c:
                continue
            r = self.resolve(c, d)
            if not r:
                continue
            if r[0] == "f":
                self.add(r[1])
            elif r[1].startswith("tools/tests/") and r[1] != "tools/tests":
                self.add_dir(r[1])          # hostshim などの差し替えディレクトリ
        for a, b in PY_IMPORT.findall(text):
            for name in ([a] if a else [x.strip() for x in b.split(",")]):
                name = name.split(" as ")[0].strip()
                if not name:
                    continue
                mod = name.replace(".", "/")
                for base in (d, "tools", "tools/tests", ""):
                    r = norm(os.path.join(base, mod + ".py"))
                    if is_tracked_file(r):
                        self.add(r)
                        break

    def from_recipe(self, lines):
        for line in lines:
            line = re.sub(r"\$\([A-Z_]+\)", "", line).lstrip("@-")
            try:
                toks = shlex.split(line)
            except ValueError:
                toks = line.split()
            for i, t in enumerate(toks):
                if t == "-s" and i + 1 < len(toks) and "-p" in toks:
                    pat = toks[toks.index("-p") + 1]
                    rx = glob_re(norm(os.path.join(toks[i + 1], pat)))
                    for f in tracked():
                        if rx.match(f):
                            self.add(f)
                elif "=" in t and t.split("=", 1)[0].isupper():
                    continue
                elif t.startswith("-I"):
                    self.add_inc_dir(t[2:])
                elif is_tracked_file(norm(t)):
                    self.add(norm(t))


_HDR_DIRS = None


def header_dirs():
    """追跡されている .h / .inc を直下に持つディレクトリ (-I の探索先の候補)。"""
    global _HDR_DIRS
    if _HDR_DIRS is None:
        _HDR_DIRS = {os.path.dirname(f) for f in tracked()
                     if f.endswith((".h", ".inc")) and "/" in f}
    return _HDR_DIRS


def extract(rules, target):
    ex = Extractor()
    ex.from_recipe(rules.get(target, []))
    ex.drain()
    return ex.found


# ---------------------------------------------------------------- --lint
def lint():
    m = load_map()
    rules, vars_ = read_makefiles()
    par = check_lists(vars_)
    listed = set(par)
    mapped = set(m["checks"])
    errs = []
    for t in sorted(listed - mapped):
        errs.append("対応表に無い検査: %s (tools/check_map.yaml の checks: に足す)" % t)
    for t in sorted(mapped - listed):
        errs.append("列に無い検査が対応表にある: %s" % t)
    for key in ("broad", "docs_always"):
        for t in m[key]:
            if t not in listed:
                errs.append("%s: に列に無い検査がある: %s" % (key, t))
    trk = tracked()
    full = compile_globs(m["full"])
    ign = compile_globs(m["ignore"])
    notest = compile_notest(m["notest"])
    for g, r, ex in notest:
        if not any(r.match(f) for f in trk):
            errs.append("notest: glob %r が追跡されているどのファイルにも当たらない" % g)
        for x in ex:
            if not any(x.match(f) for f in trk):
                errs.append("notest: %r の except %r が古い" % (g, x.pattern))
    for f in sorted(trk):
        if is_notest(f, notest) and matches(f, full):
            errs.append("notest: と full: の両方に当たる: %s" % f)
    leaks = 0
    for t in sorted(mapped & listed):
        globs = m["checks"][t] or []
        if not globs:
            errs.append("%s: glob が空" % t)
            continue
        cg = compile_globs(globs)
        for g, r in cg:
            if not any(r.match(f) for f in trk):
                errs.append("%s: glob %r が追跡されているどのファイルにも当たらない"
                            % (t, g))
        for f in sorted(extract(rules, t)):
            if is_notest(f, notest):
                errs.append("%s: 入力 %s が notest なのに入力になっている "
                            "(notest: を狭めるか外す)" % (t, f))
            if not (matches(f, cg) or matches(f, full) or matches(f, ign)):
                errs.append("%s: 入力 %s が glob に入っていない (漏れ)" % (t, f))
                leaks += 1
    if errs:
        sys.stderr.write("check_select --lint: %d 件\n" % len(errs))
        for e in errs:
            sys.stderr.write("  " + e + "\n")
        return 1
    print("check-map: 検査 %d 本、対応表の漏れ 0 件" % len(listed))
    return 0


# ---------------------------------------------------------------- --select
def changed_files(base):
    """(コミット済みの変更, 未コミット + 追跡外) を返す。改名は旧名も数える。"""
    committed = set(git("diff", "--name-only", "--no-renames", "%s...HEAD" % base))
    work = set(git("diff", "--name-only", "--no-renames", "HEAD"))
    work |= set(git("ls-files", "--others", "--exclude-standard"))
    return sorted(committed), sorted(work - committed)


def default_base():
    """(基点, 説明)。既定は feat/gui との merge-base。

    feat/gui の上でコミットした後は merge-base == HEAD になり、コミット済みの
    変更が 1 件も見えず「変更なし = check-fast」に退化する。そのときは HEAD~1
    (直前のコミット) を基点にする (代行レビュー P2-3)。
    """
    head = git("rev-parse", "HEAD")[0]
    for ref in ("feat/gui", "origin/feat/gui", "main"):
        p = subprocess.run(["git", "-C", ROOT, "merge-base", ref, "HEAD"],
                           capture_output=True, text=True)
        mb = p.stdout.strip()
        if p.returncode != 0 or not mb:
            continue
        if mb != head:
            return mb, "%s との merge-base" % ref
        p = subprocess.run(["git", "-C", ROOT, "rev-parse", "--verify", "-q",
                            "HEAD~1"], capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip():
            return (p.stdout.strip(),
                    "HEAD~1 (%s との merge-base が HEAD — %s の上でコミット済み)"
                    % (ref, ref))
        return head, "HEAD (%s の上、親コミットが無い)" % ref
    return head, "HEAD (feat/gui / main が見つからない)"


def plan(files, base=None):
    """変更の一覧から (mode, stage, mut, 説明の行) を決める。

    base があるときだけ Makefile / build/*.mk の基点版を git から読んで型に当てる
    (make_narrow)。

    stage は回す検査 (make の目標)、mut はそのうち変異込みで回す検査。"""
    m = load_map()
    _, vars_ = read_makefiles()
    par = check_lists(vars_)
    ign = compile_globs(m["ignore"])
    full = compile_globs(m["full"])
    docs = compile_globs(m["docs_only"])
    notest = compile_notest(m["notest"])
    mk = compile_globs(MAKE_INPUT_GLOBS)
    broad = set(m["broad"])
    checks = {t: compile_globs(g) for t, g in m["checks"].items()}
    # 走査型の検査 (broad:) の `**` を含む glob は「表に載っている」の判定に数えない
    # — ツリーを丸ごと舐める検査に当たっただけで安全側が発火しなくなるのを防ぐ
    # (代行レビュー P2-1 の保険)。変異込みで回す検査を選ぶ方には数える。
    cover = {t: [(g, r) for g, r in cg if not (t in broad and "**" in g)]
             for t, cg in checks.items()}
    changed = [f for f in files if not matches(f, ign)]

    lines = []
    hit, unmatched, full_hits, notest_hits, mk_hits = set(), [], [], [], []
    for f in changed:
        hit |= {t for t, cg in checks.items() if matches(f, cg)}
        if matches(f, full):
            if matches(f, mk):
                mk_hits.append(f)
            else:
                full_hits.append(f)
                lines.append("%s: 全部 — 全体に影響するファイル" % f)
        elif is_notest(f, notest):
            notest_hits.append(f)
        elif not matches(f, docs) and \
                not any(matches(f, cg) for cg in cover.values()):
            unmatched.append(f)
    if mk_hits and not full_hits and not unmatched:
        # Makefile / build/*.mk は「新しい試験を足す形」だけ型の完全一致で絞る
        # (1 回で全部の変更ファイルを見る)。
        narrowed, why = make_narrow(base, mk_hits, checks)
        if narrowed is None:
            full_hits += mk_hits
            lines.append("%s: 全部 — %s" % (" ".join(mk_hits), why))
        else:
            hit |= narrowed & set(par)
            lines.append("%s: 絞り込み — %s (%s)" %
                         (" ".join(mk_hits), why,
                          " ".join(sorted(narrowed & set(par))) or "追加選択なし"))
    elif mk_hits:
        full_hits += mk_hits          # 他の理由で全部になるので make は回さない

    if not changed:
        mode, st, mu = "fast", list(par), []
        lines.append("変更なし → 全部を変異なしで回す (= check-fast)")
    elif full_hits or unmatched:
        mode, st, mu = "full", list(par), list(par)
        lines += ["full: に当たる: %s" % f for f in full_hits[:10]]
        lines += ["対応表に無い (broad の ** を除く): %s" % f for f in unmatched[:10]]
        lines.append("→ 安全側: 全部を変異込みで回す (= check)")
    elif not notest_hits and all(matches(f, docs) for f in changed):
        # 文書を読む検査は当たらなくても回す (表の漏れで文書の検査を落とさない)。
        run = hit | (set(m["docs_always"]) & set(par))
        mode = "docs"
        st = [t for t in par if t in run]
        mu = list(st)
        lines.append("docs だけの変更 → %d 本だけ回す (当たった検査 + docs_always:): %s"
                     % (len(run), " ".join(sorted(run))))
    elif not hit:
        mode, st, mu = "fast", list(par), []
        lines += ["notest: に当たる: %s" % f for f in notest_hits[:10]]
        lines.append("→ 変異の選び方に足す変更が無い: 全部を変異なしで回す "
                     "(= check-fast)")
    else:
        mode = "sel"
        st = list(par)
        mu = [t for t in par if t in hit]
        if notest_hits:
            lines.append("notest: に当たる %d 件は変異の選び方に足さない"
                         % len(notest_hits))
        lines.append("変異込み %d 本: %s" % (len(mu), " ".join(sorted(mu))))
        lines.append("残り %d 本は変異なし" % (len(st) - len(mu)))
    return mode, st, mu, lines


def select(base, files=None, base_note=""):
    if files is None:
        committed, work = changed_files(base)
    else:
        committed, work = [], list(files)
    mode, st, mu, lines = plan(sorted(set(committed) | set(work)),
                               base=None if files is not None else base)
    summary = ("check-changed: 基点 %s (%s)、コミット済みの変更 %d 件 + 未コミット %d 件 → %s"
               % (base[:12], base_note or "指定", len(committed), len(work), mode))
    for l in lines:
        sys.stderr.write("  " + l + "\n")
    sys.stderr.write("*** " + summary + " ***\n")
    q = lambda xs: shlex.quote(" ".join(xs))
    print("CC_MODE=%s" % mode)
    print("CC_STAGE1=%s" % q(st))
    print("CC_MUT1=%s" % q(mu))
    print("CC_SUMMARY=%s" % shlex.quote(summary))
    return 0


# ---------------------------------------------------------------- 下書き
def suggest(targets):
    rules, _ = read_makefiles()
    out = {}
    for t in targets:
        fs = sorted(extract(rules, t))
        out[t] = fs
    sys.stdout.write(yaml.safe_dump({"checks": out}, allow_unicode=True,
                                    sort_keys=False, default_flow_style=False))
    return 0


def main(argv):
    if "--lint" in argv:
        return lint()
    if "--select" in argv:
        base = argv[argv.index("--base") + 1] if "--base" in argv else ""
        if "--files" in argv:                 # 試しに選ばせる (git を見ない)
            files = argv[argv.index("--files") + 1:]
            return select("(--files)", files, "git を見ない")
        if base:
            return select(base, None, "BASE=%s" % base)
        b, note = default_base()
        return select(b, None, note)
    if "--inputs" in argv:
        rules, _ = read_makefiles()
        for f in sorted(extract(rules, argv[argv.index("--inputs") + 1])):
            print(f)
        return 0
    if "--suggest" in argv:
        return suggest(argv[argv.index("--suggest") + 1:])
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
