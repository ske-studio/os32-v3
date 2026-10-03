#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Select check mutations from changed inputs; see docs/08_build.md §8-4.

--select [--base REF | --files PATH...] emits shell assignments.
--lint checks per-check tools/check_map.d/*.yaml plus automatic C headers.
--inputs TARGET includes gcc -MM/static dependencies; --suggest TARGET emits
manual source/script/data entries without headers. Global policy stays in
check_map.yaml. Selection is a work-in-progress shortcut; full make check is
required after integration.
"""
import os
import glob
from functools import lru_cache
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
    for path in sorted(glob.glob(os.path.join(os.path.dirname(MAP_PATH), "check_map.d", "*.yaml"))):
        with open(path, encoding="utf-8") as f:
            shard = yaml.safe_load(f)
        name = os.path.basename(path)[:-5]
        if not isinstance(shard, dict) or set(shard) != {name}:
            raise SystemExit("check-map: shard name mismatch: " + path)
        if name in m.get("checks", {}):
            raise SystemExit("check-map: duplicate: " + name)
        m.setdefault("checks", {}).update(shard)
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
ROUTED_BUILD = {"build/kernel.mk", "build/libs.mk", "build/programs.mk"}
CHECK_CONTROL = re.compile(r"check-|MUT|HOST32|\.ONESHELL|export|override|CHECK_PAR|\bBASE\b|mut_on|host32_check", re.IGNORECASE)

# Outputs of sdk/gen_kapi.py and sdk/kapi_rust_gen.py, including rewritten link scripts.
KAPI_GENERATED = (
    "sdk/include/os32/os32_kapi_generated.h", "sdk/include/os32/os32_kapi_slots.h",
    "kapi/kapi_generated.c", "exec/exec_kapi_init.inc",
    "sdk/include/os32/os32_generations.h", "sdk/os32_generations.py",
    "sdk/rust/os32api/src/generations.rs", "sdk/include/os32/os32_unit_stamp.h",
    "sdk/crt/generations.inc", "sdk/link/generations.ld",
    "sdk/link/app.ld", "sdk/link/app_sys.ld", "sdk/link/shlib.ld", "build/os32.ld",
    "sdk/rust/os32api/src/kapi_generated.rs",
)


def routed_build_safe(base, path):
    """Only route build changes whose added AND removed lines contain no check controls."""
    if base is None:
        return False
    try:
        ancestor = git("merge-base", base, "HEAD")[0]
        before = subprocess.run(["git", "-C", ROOT, "show", ancestor + ":" + path],
                                capture_output=True, text=True)
        if before.returncode:
            return False
        with open(os.path.join(ROOT, path), encoding="utf-8") as f:
            after = f.read()
    except (OSError, IndexError):
        return False
    import difflib
    delta = difflib.ndiff(before.stdout.splitlines(), after.splitlines())
    return not any(CHECK_CONTROL.search(line[2:]) for line in delta
                   if line.startswith(("+ ", "- ")))


MAKE_INPUT_GLOBS = ("Makefile", "build/*.mk", "build/checks.d/*.mk")


def makefile_paths():
    """ROOT にある MAKE_INPUT_GLOBS のファイル (追跡外も含む)。"""
    out = []
    if os.path.isfile(os.path.join(ROOT, "Makefile")):
        out.append("Makefile")
    bdir = os.path.join(ROOT, "build")
    if os.path.isdir(bdir):
        out += sorted("build/" + f for f in os.listdir(bdir) if f.endswith(".mk"))
    out += sorted(os.path.relpath(p, ROOT) for p in
                  glob.glob(os.path.join(ROOT, "build/checks.d/*.mk")))
    return out


def read_makefiles():
    """{検査名: [recipe 行]} と {変数名: 値} を返す (check-* の規則だけ)。

    --lint / --inputs / --suggest が recipe から試験スクリプトを辿るための
    字面の読みで、make の意味論は持たない (検査列の名前と、タブ行の字面だけ)。
    変異の選び方で build/*.mk の差を判定するのはここではなく make_narrow (型の一致)。"""
    rules, vars_, order = {}, {}, []
    for mf in makefile_paths():
        with open(os.path.join(ROOT, mf), encoding="utf-8") as f:
            lines = f.read().split("\n")
        i, cur = 0, None
        while i < len(lines):
            line = lines[i]
            while line.endswith("\\") and i + 1 < len(lines):
                i += 1
                line = line[:-1] + " " + lines[i].strip()
            if line.startswith("CHECK_PAR_ORDER += "):
                order.extend(line.split("+=", 1)[1].split())
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
    if order:
        vars_["CHECK_PAR_TARGETS"] = [x.split(":", 1)[1] for x in sorted(order)]
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
#            基点のどの make ファイルにも現れない。規則は 1 つだけ、recipe は 1 行以上。
#            **ファイル末尾の塊** (基点の最後の行より後ろ) にだけ置ける — 規則とその recipe・
#            コメント・空行がそこに続く。基点の行のあいだに入った新規則は全部
#       (c)  TPL_RECIPE_RE  `\tpython3 -B tools/tests/<file>.py [--flag ...] [$(MUT)|$(MUTS)]`
#            — 持ち主は、上へ tab 行だけを辿って着く基点の列にある検査の基点の規則の行
#            `check-<name>:…` (`=` を含まない)。その検査の recipe は **規則行の直後から連続する
#            tab 行だけ** (途中に基点のコメント・空行・条件ディレクティブなど非 tab 行を含まず、
#            連続の後ろにそれらを挟んで tab 行が続かない — make はそれらで recipe を切らない)。
#            足す位置はその連続の中か直後。script は作業中の木にあり、対応表の当該検査の
#            glob に当たる。末尾の塊の新しい規則の後ろの recipe 行はその規則のもの
#       (d)  空行 (完全に空) と `#` 始まりのコメント行 (末尾 `\` なし) — 末尾の塊の中か、
#            前後どちらにも基点の tab 行が接していない基点の 2 行のあいだだけ (recipe の途中・
#            規則と recipe のあいだには入れない)
#     `.PHONY: 新名` は末尾に足した新規則のrecipe後だけ。以後のtab行は拒否。
#     (b)(c)(d) の置き場所の制限は「持ち主が変わる配置を型から締め出す」(ユーザー決定
#     2026-10-01、独立レビュー 2〜3 回目: 基点のコメント・空行・ifeq 越しの横取り)
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
TPL_HOST32_RE = re.compile(r"^\t\$\(call host32_check,([a-z0-9_]+\.py)\)$")
TPL_PHONY_RE = re.compile(r"^\.PHONY: (%s)$" % NAME)
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


# 足した行に許さない制御文字 (LF 以外の行境界に見える文字 — VT / FF / CR / \x1c-\x1e /
# NEL / LS / PS — と、その他の制御文字。tab は型の先頭にだけ現れる)
CTRL_RE = re.compile("[\x00-\x08\x0b-\x1f\x7f\x85\u2028\u2029]")


def split_lf(data):
    """bytes を LF ('\\n') だけで行に分ける (str.splitlines() は VT / FF / CR / \x1c-\x1e /
    NEL なども行境界にして実際の行の変更を消す — 独立レビュー 4 回目)。末尾の LF 1 つは
    行に数えない (末尾の改行の有無は差に数えない)。"""
    text = data.decode("utf-8", "replace")
    if text.endswith("\n"):
        text = text[:-1]
    return text.split("\n")


def continued(text):
    """make の継続規則: 末尾の `\\` が奇数個なら次の物理行と結ぶ (recipe の行も同じ)。"""
    n = len(text) - len(text.rstrip("\\"))
    return n % 2 == 1


def logical_lines(lines):
    """物理行の列を make の論理行 [(start, end, 結合した文字列)] にする (continued())。"""
    out, i = [], 0
    while i < len(lines):
        s, text = i, lines[i]
        while continued(text) and i + 1 < len(lines):
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


# make が recipe を切らずに飛ばす (または飛ばすかもしれない) 非 tab 行。(2) の「recipe が
# 連続する tab 行だけ」の判定で、連続の後ろにこれらを挟んで tab 行が続けば recipe が続いて
# いると見なす (多めに数える = 全部に倒す側)。
RECIPE_PASS_RE = re.compile(
    r"^(?:ifeq|ifneq|ifdef|ifndef|else|endif|export|unexport|vpath|undefine|override|"
    r"define|endef|-?include|sinclude)\b")


def _passes_recipe(text):
    return _is_skippable(text) or RECIPE_PASS_RE.match(text.strip()) is not None


def classify_file(rel, work, inserted, base_words, new_names, m_checks):
    """1 ファイルの足した行を型に当てる。{検査名: [script]} ((b) は新しい名前、(c) は
    基点の列の名前) と {新しい名前: 規則の数} を返す。合わなければ Reject。
    inserted は inserted_lines() の戻り (列の物理行は入っていない)。

    持ち主が変わる配置を型から締め出す (ユーザー決定 2026-10-01、独立レビュー 3 回目):
      (1) 新しい規則 (b) とその recipe・コメント・空行は **ファイル末尾の塊** (基点の最後の
          行より後ろに続く足した行) だけ。基点の行のあいだに入った新規則は全部
      (2) 既存の検査への行 (c) は、その検査の recipe が **規則行の直後から連続する tab 行
          だけ** (途中に基点のコメント・空行・条件ディレクティブなど非 tab 行を含まず、連続の
          後ろにそれらを挟んで tab 行が続かない) のときだけ。足す位置はその連続の中か直後
      (3) コメント・空行 (d) は、(1) の末尾の塊の中か、前後どちらにも基点の tab 行が接して
          いない基点の 2 行のあいだだけ"""
    ll = logical_lines(work)
    ok = depth_flags(ll)
    phys2log = {}
    for k, (s, e, _) in enumerate(ll):
        for i in range(s, e + 1):
            phys2log[i] = k
    ins = set(inserted)
    kept = [i for i in range(len(work)) if i not in ins]
    tail_start = (kept[-1] + 1) if kept else 0      # ここから後ろは全部足した行
    picked, headers = {}, {}
    cur_new = None          # 末尾の塊で直前に足した新しい規則 (以後の recipe 行の持ち主)

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
        if CTRL_RE.search(line):
            raise Reject("%s: 制御文字を含む行" % where)
        in_tail = i >= tail_start
        if line == "" or TPL_COMMENT_RE.match(line):
            if not in_tail:
                p = max((j for j in kept if j < i), default=None)
                n = min((j for j in kept if j > i), default=None)
                if (p is not None and work[p].startswith("\t")) or \
                        (n is not None and work[n].startswith("\t")):
                    raise Reject("%s: 基点の recipe 行に接してコメント / 空行を足している" % where)
            continue
        mh = TPL_HEADER_RE.match(line)
        mr = TPL_RECIPE_RE.match(line) or TPL_HOST32_RE.match(line)
        mp = TPL_PHONY_RE.match(line)
        if mp:
            if not in_tail or mp.group(1) != cur_new or not picked.get(cur_new):
                raise Reject("%s: .PHONY must follow its new rule and recipe" % where)
            cur_new = None
            continue
        if mh:
            name = mh.group(1)
            if not in_tail:
                raise Reject("%s: 新しい規則 %s がファイル末尾の塊にない (基点の行のあいだ)"
                             % (where, name))
            if name not in new_names:
                raise Reject("%s: 規則 %s の名前が列に足した新しい名前でない" % (where, name))
            headers[name] = headers.get(name, 0) + 1
            picked.setdefault(name, [])
            cur_new = name
        elif mr:
            if cur_new is not None:
                name = cur_new                      # (1) 末尾の塊の新しい規則の recipe
            else:
                # (2) 持ち主: 上へ tab 行だけを辿って基点の検査の規則の行に着く
                j = i - 1
                while j >= 0 and work[j].startswith("\t"):
                    j -= 1
                t = work[j] if j >= 0 else ""
                mo = OWNER_RE.match(t)
                if j < 0 or j in ins or not mo or "=" in t:
                    raise Reject("%s: 持ち主が基点の検査の規則の行でない (recipe は規則行の直後から"
                                 "連続する tab 行だけ): %r" % (where, t[:60]))
                # 規則行は基点の独立した論理行の先頭 (直前の物理行が継続で終わっていない —
                # `\t@echo \\` の次の `check-a:` は echo の続き。独立レビュー 4 回目) で、
                # それ自身も継続で終わらない
                if (j > 0 and continued(work[j - 1])) or continued(t):
                    raise Reject("%s: 持ち主候補 %r が継続行の途中" % (where, t[:60]))
                name = mo.group(1)
                if name not in base_words:
                    raise Reject("%s: 持ち主 %s が基点の列にない" % (where, name))
                # 連続の後ろ: make が recipe を切らない行を挟んで tab 行が続けば recipe が続く
                j = i + 1
                while j < len(work) and work[j].startswith("\t"):
                    j += 1
                while j < len(work) and _passes_recipe(work[j]):
                    j += 1
                if j < len(work) and work[j].startswith("\t"):
                    raise Reject("%s: %s の recipe がコメント・空行・ディレクティブ越しに続いている"
                                 % (where, name))
            script = "tools/tests/" + mr.group(1)
            if not os.path.isfile(os.path.join(ROOT, script)):
                raise Reject("%s: %s が木に無い" % (where, script))
            if not matches(script, m_checks.get(name, [])):
                raise Reject("%s: %s が対応表の %s の glob に入っていない" % (where, script, name))
            picked.setdefault(name, []).append(script)
        else:
            raise Reject("%s: 型に合わない行: %r" % (where, line[:60]))
    for name in headers:
        if not picked.get(name):
            raise Reject("%s: 規則 %s に recipe が無い" % (rel, name))
    return picked, headers


def _base_texts(ancestor):
    """基点のコミットにある MAKE_INPUT_GLOBS のファイル {相対パス: 行の列}。
    行は split_lf() (LF だけで分け、末尾の改行の有無は差に数えない)。"""
    mk = compile_globs(MAKE_INPUT_GLOBS)
    p = subprocess.run(["git", "-C", ROOT, "ls-tree", "-r", "--name-only", ancestor,
                        "--", "Makefile", "build"], capture_output=True, text=True)
    if p.returncode != 0:
        raise Reject("基点 %s の木を読めない" % ancestor[:12])
    rels = [rel for rel in p.stdout.splitlines() if matches(rel, mk)]
    # 1 回の cat-file --batch で全部読む (ファイルごとの git show より速い)
    q = subprocess.run(["git", "-C", ROOT, "cat-file", "--batch"], capture_output=True,
                       input="".join("%s:%s\n" % (ancestor, rel) for rel in rels).encode())
    if q.returncode != 0:
        raise Reject("基点 %s の版を読めない" % ancestor[:12])
    out, data, pos = {}, q.stdout, 0
    for rel in rels:
        nl = data.find(b"\n", pos)
        head = data[pos:nl].split()
        if nl < 0 or len(head) != 3 or head[1] != b"blob":
            raise Reject("基点版 %s を読めない" % rel)
        size = int(head[2])
        out[rel] = split_lf(data[nl + 1:nl + 1 + size])
        pos = nl + 1 + size + 1
    return out


def _work_texts():
    out = {}
    for rel in makefile_paths():
        with open(os.path.join(ROOT, rel), "rb") as f:
            out[rel] = split_lf(f.read())
    return out


SHARD_REG = re.compile(r"^CHECK_PAR_ORDER \+= ([0-9]{3,}):(check-[a-z0-9-]+)$")


def shard_layout(texts):
    registrations = {}
    for rel, lines in texts.items():
        for i, line in enumerate(lines):
            if line.startswith("CHECK_PAR_ORDER +="):
                match = SHARD_REG.fullmatch(line)
                if not match or rel != "build/checks.d/" + match[2] + ".mk":
                    raise Reject("invalid check registration: " + rel)
                if match[2] in registrations:
                    raise Reject("duplicate check registration: " + match[2])
                registrations[match[2]] = (rel, i, match[1])
    return registrations


def narrow_shards(bt, wt, mk_hits, m_checks):
    before, after = shard_layout(bt), shard_layout(wt)
    if not set(before) <= set(after):
        raise Reject("check registration removed")
    new = set(after) - set(before)
    for name in before:
        if before[name] != after[name]:
            raise Reject("check registration moved or reordered: " + name)
    for name in new:
        if any(re.search(r"(?<![\w-])" + re.escape(name) + r"(?![\w-])", line)
               for lines in bt.values() for line in lines):
            raise Reject("new check name already occurs: " + name)
    picked, headers = {}, {}
    for rel in mk_hits:
        if rel not in wt:
            raise Reject("make file removed: " + rel)
        if rel not in bt and rel not in {after[n][0] for n in new}:
            raise Reject("unregistered make file added: " + rel)
        b, w = bt.get(rel, []), wt[rel]
        # Existing registration lines stay byte-for-byte in place. New ones are
        # validated above, then omitted from the recipe ownership analysis.
        w = [line for line in w if not (SHARD_REG.fullmatch(line) and
                                       SHARD_REG.fullmatch(line)[2] in new)]
        ins = inserted_lines(b, w, set(), set())
        if ins is None:
            raise Reject("%s に削除・変更行がある" % rel)
        pk, hd = classify_file(rel, w, ins, set(before), new, m_checks)
        picked.update(pk)
        for name, count in hd.items():
            headers[name] = headers.get(name, 0) + count
    if set(headers) != new or any(n != 1 for n in headers.values()):
        raise Reject("new registrations and rules differ")
    return set(picked), "型に一致: 新しい検査 %d 本、行を足した検査 %d 本" % (len(new), len(set(picked) - new))


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
        if any(rel.startswith("build/checks.d/") for rel in bt):
            if any(".ONESHELL" in line for lines in wt.values() for line in lines):
                raise Reject(".ONESHELL がある")
            return narrow_shards(bt, wt, mk_hits, m_checks)
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
BARE_EXTS = (".md", ".yaml", ".yml", ".json", ".tsv", ".c", ".h", ".inc")


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
        # Inline C snippets used by compiler probes have the same include search
        # roots as the script's compiler invocation.
        for inc in C_INC.findall(text):
            for base in sorted(self.inc_dirs | {"", d}):
                rel = norm(os.path.join(base, inc))
                if is_tracked_file(rel):
                    self.add(rel)
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
            macro = TPL_HOST32_RE.match("\t" + line.strip())
            if macro:
                self.add("tools/tests/" + macro.group(1))
                continue
            line = re.sub(r"\$\([A-Z_]+\)", "", line).lstrip("@-")
            try:
                toks = shlex.split(line)
            except ValueError:
                toks = line.split()
            for i, t in enumerate(toks):
                t = t.rstrip(";")
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


@lru_cache(maxsize=512)
def compiler_headers(root, sources, inc_dirs):
    """Compiler dependency scan, supplemented by static includes for inactive branches.

    -MG keeps generated/missing includes visible; only repository files count.
    The conservative static closure is retained when a host compiler cannot
    preprocess a target-only translation unit (asm/ABI/vendor configuration).
    """
    found = set()
    for source in sources:
        cmd = ["gcc", "-MM", "-MG", "-I."] + ["-I" + d for d in inc_dirs] + [source]
        proc = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
        if proc.returncode:
            continue
        deps = proc.stdout.replace("\\\n", " ").split(":", 1)[-1]
        for dep in shlex.split(deps):
            rel = norm(os.path.relpath(dep, root) if os.path.isabs(dep) else dep)
            if rel.endswith(".h") and os.path.isfile(os.path.join(root, rel)):
                found.add(rel)
    return frozenset(found)


def extract(rules, target, seeds=()):
    ex = Extractor()
    ex.from_recipe(rules.get(target, []))
    for source in seeds:
        if source.endswith((".c", ".py")) and not any(c in source for c in "*?["):
            ex.add(source)
    ex.drain()
    ex.found.update(compiler_headers(ROOT, tuple(sorted(f for f in ex.found if f.endswith(".c"))),
                                    tuple(sorted(ex.inc_dirs))))
    return ex.found


def effective_checks(m, rules):
    return {t: list(g or []) + sorted(f for f in extract(rules, t, g or []) if f.endswith(".h"))
            for t, g in m["checks"].items()}


# ---------------------------------------------------------------- --lint
def lint():
    m = load_map()
    rules, vars_ = read_makefiles()
    par = check_lists(vars_)
    listed = set(par)
    mapped = set(m["checks"])
    errs = []
    for t in sorted(listed - mapped):
        errs.append("対応表に無い検査: %s (tools/check_map.d/ に足す)" % t)
    for t in sorted(mapped - listed):
        errs.append("列に無い検査が対応表にある: %s" % t)
    for key in ("broad", "docs_always", "artifact_readers"):
        for t in m.get(key, []):
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
    effective = effective_checks(m, rules)
    leaks = 0
    for t in sorted(mapped & listed):
        globs = m["checks"][t] or []
        if not globs:
            errs.append("%s: glob が空" % t)
            continue
        cg = compile_globs(effective[t])
        for g, r in cg:
            if not any(r.match(f) for f in trk):
                errs.append("%s: glob %r が追跡されているどのファイルにも当たらない"
                            % (t, g))
        for f in sorted(extract(rules, t)):
            if is_notest(f, notest):
                errs.append("%s: 入力 %s が notest なのに入力になっている "
                            "(notest: を狭めるか外す)" % (t, f))
            if not (matches(f, cg) or (f not in ROUTED_BUILD and matches(f, full)) or matches(f, ign)):
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
    """基準枝自身では直前のコミット、作業枝では merge-base を検査する。"""
    head = git("rev-parse", "HEAD")[0]
    branch = git("rev-parse", "--abbrev-ref", "HEAD")[0]
    refs = load_map().get("base_refs", ["main", "origin/main", "feat/gui", "origin/feat/gui"])
    branches = {ref.removeprefix("origin/") for ref in refs}
    if branch in branches:
        p = subprocess.run(["git", "-C", ROOT, "rev-parse", "--verify", "-q", "HEAD~1"],
                           capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip():
            return p.stdout.strip(), "HEAD~1 (%s の上でコミット済み)" % branch
        return head, "HEAD (%s の上、親コミットが無い)" % branch
    for ref in refs:
        p = subprocess.run(["git", "-C", ROOT, "merge-base", ref, "HEAD"],
                           capture_output=True, text=True)
        mb = p.stdout.strip()
        if p.returncode != 0 or not mb:
            continue
        if mb != head:
            return mb, "%s との merge-base" % ref
        return head, "HEAD (%s と同じ基点、worktree の変更だけ)" % ref
    return head, "HEAD (基準枝が見つからない)"


def plan(files, base=None):
    """変更の一覧から (mode, stage, mut, 説明の行) を決める。

    base があるときだけ Makefile / build/*.mk の基点版を git から読んで型に当てる
    (make_narrow)。

    stage は回す検査 (make の目標)、mut はそのうち変異込みで回す検査。"""
    m = load_map()
    rules, vars_ = read_makefiles()
    par = check_lists(vars_)
    ign = compile_globs(m["ignore"])
    full = compile_globs(m["full"])
    docs = compile_globs(m["docs_only"])
    notest = compile_notest(m["notest"])
    mk = compile_globs(MAKE_INPUT_GLOBS)
    broad = set(m["broad"])
    # Only header changes (or KAPI regeneration) need compiler dependencies.
    # Source/script changes use the explicit map; lint always checks both.
    inputs = effective_checks(m, rules) if any(
        f.endswith(".h") or f == "sdk/kapi.json" for f in files) else m["checks"]
    checks = {t: compile_globs(g) for t, g in inputs.items()}
    # 走査型の検査 (broad:) の `**` を含む glob は「表に載っている」の判定に数えない
    # — ツリーを丸ごと舐める検査に当たっただけで安全側が発火しなくなるのを防ぐ
    # (代行レビュー P2-1 の保険)。変異込みで回す検査を選ぶ方には数える。
    cover = {t: [(g, r) for g, r in cg if not (t in broad and "**" in g)]
             for t, cg in checks.items()}
    changed = [f for f in files if not matches(f, ign)]
    # Build flags affect scanners and checks consuming built artifacts. KAPI input
    # reaches readers through generated files even before regeneration.
    artifact_targets = set(m.get("artifact_readers", [])) | {t for t, globs in m["checks"].items()
                        if any(g.startswith(("build/out/", "build/sdk/")) for g in globs)}
    generated = KAPI_GENERATED + ("sdk/gen_kapi.py", "sdk/kapi_rust_gen.py")

    lines = []
    hit, unmatched, full_hits, notest_hits, mk_hits = set(), [], [], [], []
    for f in changed:
        hit |= {t for t, cg in checks.items() if matches(f, cg)}
        if f in ROUTED_BUILD:
            if not routed_build_safe(base, f):
                full_hits.append(f)
                lines.append("%s: 全部 — 検査制御の変更または基点版なし" % f)
                continue
            hit |= broad | artifact_targets
            lines.append("%s: build scanners + artifact readers" % f)
            continue
        if f == "sdk/kapi.json":
            hit |= artifact_targets
            hit |= {t for t, cg in checks.items() if "kapi" in t or
                    any(matches(dep, cg) for dep in generated)}
            lines.append("sdk/kapi.json: generated KAPI readers")
            continue
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
        fs = sorted(f for f in extract(rules, t, load_map()["checks"].get(t, [])) if not f.endswith(".h"))
        out[t] = fs
    sys.stdout.write(yaml.safe_dump(out, allow_unicode=True,
                                    sort_keys=False, default_flow_style=False))
    return 0


def dialect_variants_needed(files=None):
    """The nine make environment/override cases only depend on build/checker code."""
    if files is None:
        base, _ = default_base()
        committed, work = changed_files(base)
        files = committed + work
    patterns = compile_globs(["Makefile", "build/**/*.mk", "tools/check_c_dialect.py",
                              "tools/clang_ast/**", "tools/tests/test_c_dialect.py",
                              "tools/check_select.py", "sdk/example/hello/Makefile",
                              "tools/tests/mutpar.py"])
    return any(matches(f, patterns) for f in files)


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
        target = argv[argv.index("--inputs") + 1]
        for f in sorted(extract(rules, target, load_map()["checks"].get(target, []))):
            print(f)
        return 0
    if "--suggest" in argv:
        return suggest(argv[argv.index("--suggest") + 1:])
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
