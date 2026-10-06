#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_tests_inventory.py — 試験一覧の生成と登録検査

試験の一覧を人が手で書くと、ターゲットを足した日から腐りはじめる。実際
docs/archive/TEST_INVENTORY_2026-09-14.md は「`make check` は 31 ターゲット」と
書いた 1 日後に 43 ターゲットになった。そこで**機械で読める事実は機械に書かせ、
人にしか書けない判断だけを手で持つ**という分け方にする。

  機械が書く: `check:` の列、各ターゲットのコマンド、対象ソース、対応する
              RED→GREEN の記録 (`tools/tests/*_tdd.md`)、その記録が指す票、CI 被覆
  人が書く:   なぜその試験が弱いか、何を消すべきか、何が試験されていないか
              (docs/TESTS.md の `<!-- manual:… -->` 区間。生成器はここを保つ)

情報源は build/*.mk と試験スクリプト・ハーネスそのもので、この生成器は
どこにも一覧を持たない。Makefile に 1 行足せば表が増える。

使い方:
    python3 tools/gen_tests_inventory.py            # 標準出力に出す
    python3 tools/gen_tests_inventory.py --write    # build/out/TESTS.md と HOST32.md を書く
    python3 tools/gen_tests_inventory.py --check    # 登録不備があれば 1 で終わる
                                                    #   (make check-tests-inventory)

名前の対応規則 (ターゲット → `*_tdd.md`) は 3 段。3 つとも当てて、当たったものを
**全部**並べる (1 本だけ選ぶと残りが見えなくなる)。どれも当たらなければ空欄 —
記録が無いことも事実なので隠さない。

  規則 1  実行スクリプトの冒頭 40 行 (docstring / コメント) が名指しする
          `tools/tests/<x>_tdd.md` — 「記録: …」「Log: …」「対応表は …」など
  規則 2  build/*.mk のターゲット直前のコメント塊が名指しする `<x>_tdd.md`
  規則 3  語幹の一致 — `tools/tests/test_<stem>.py` / `tools/tests/<stem>_host.c` /
          ターゲット名 `check-<stem>` に対する `tools/tests/<stem>_tdd.md`

票は当たった `*_tdd.md` の冒頭 16 行から「票」を含む行を拾い、`docs/**.md` への
リンク > バッククォートの中の `.md` > 「票 B8」の記号、の順で取る (規則 4)。
記録が無ければ票も空欄になる。

1 つのターゲットが複数のスクリプトを回す場合 (`check-memory-host` など) は、
当たった記録を**全部**並べる。1 本だけ選ぶと残りが見えなくなる。
"""

import argparse
import ast
import glob
import os
import re
import sys
import check_select

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_REL = "build/out/TESTS.md"
HOST32_REL = "build/out/HOST32.md"
MK_FILES = check_select.makefile_paths()
CI_WORKFLOW = ".github/workflows/check.yml"

# 実行スクリプトの module 直下の代入のうち、対象ソース / ハーネスを指しているもの。
# 名前に SRC を含むもの (SRC / HOST_SRC / ASM_SRC / TARGET_SRCS …) に加えてこの表。
SRC_VARS = ("SCRIPT", "TARGET_STUB", "SHIMS", "MK", "HARNESS")

# 実行スクリプトの本文に直接書かれた ROOT / "…" 形の実ソース。
ROOT_PATH = re.compile(r'ROOT\s*/\s*"([A-Za-z0-9_][A-Za-z0-9_./-]+\.(?:c|inc|rs|asm|py))"')

TDD_PATH = re.compile(r"(tools/tests/[a-z0-9_]+_tdd\.md)")
TDD_ANY = re.compile(r"([a-z0-9_]+)_tdd\.md")
DOC_LINK = re.compile(r"(docs/[A-Za-z0-9_./-]+\.md)")


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def read(relpath):
    p = os.path.join(ROOT, relpath)
    if not os.path.isfile(p):
        return None
    with open(p, encoding="utf-8") as f:
        return f.read()


# ---------------------------------------------------------------------------
#  Makefile を読む
# ---------------------------------------------------------------------------

RULE = re.compile(r"^([A-Za-z0-9_][A-Za-z0-9_.+-]*)\s*:(?!=)\s*(.*)$")


def parse_makefiles():
    """{target: {'file':…, 'line':…, 'deps':[…], 'recipe':[…], 'comment':str}}"""
    rules = {}
    for mk in MK_FILES:
        text = read(mk)
        if text is None:
            continue
        lines = text.split("\n")
        i = 0
        while i < len(lines):
            line = lines[i]
            if line.startswith("\t") or not line.strip():
                i += 1
                continue
            m = RULE.match(line)
            if not m or m.group(1) in ("PHONY", ".PHONY"):
                i += 1
                continue
            target = m.group(1)
            deps_text = m.group(2)
            rule_line = i          # 依存の行継続で i が動く前に控えておく
            # 行継続した依存
            while deps_text.rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
                deps_text = deps_text.rstrip()[:-1] + " " + lines[i]
            # 直前の連続したコメント行
            comment = []
            k = rule_line - 1
            while k >= 0 and lines[k].lstrip().startswith("#"):
                comment.append(lines[k].lstrip()[1:].strip())
                k -= 1
            comment.reverse()
            recipe = []
            i += 1
            while i < len(lines) and (lines[i].startswith("\t") or
                                      (lines[i].strip() == "" and
                                       i + 1 < len(lines) and
                                       lines[i + 1].startswith("\t"))):
                if lines[i].startswith("\t"):
                    cmd = lines[i][1:]
                    while cmd.rstrip().endswith("\\") and i + 1 < len(lines):
                        i += 1
                        cmd = cmd.rstrip()[:-1] + " " + lines[i].strip()
                    # 変異の有無は $(MUT) / $(MUTS) で切り替える (build/sdk.mk)。
                    # 表には `make check` (MUTATE=1) で回る形を出す。
                    # These recipes run the true branch under MUTATE=1.
                    # Strip the outer conditional before displaying its body;
                    # the nested $(firstword ...) remains a readable variable.
                    if cmd.startswith("$(if $(MUT),") and cmd.endswith(",@:)"):
                        cmd = cmd[len("$(if $(MUT),"):-len(",@:)")]
                    macro = check_select.TPL_HOST32_RE.match("\t" + cmd.strip())
                    if macro:
                        cmd = "HOST32_RUNNERS ごと: python3 -B tools/tests/%s --runner <runner>; 先頭で $(MUT)" % macro.group(1)
                    cmd = cmd.replace("$(MUTS)", "--mutants").replace(
                        "$(MUT)", "--mutate")
                    if 'for runner in $(HOST32_RUNNERS);' in cmd:
                        cmd = 'HOST32_RUNNERS ごと: ' + cmd[cmd.index('python3 '):].removesuffix('; done')
                    recipe.append(cmd)
                i += 1
            if target in rules and not recipe:
                continue
            rules[target] = {
                "file": mk,
                "line": rule_line + 1,
                "deps": deps_text.split(),
                "recipe": recipe,
                "comment": "\n".join(comment).strip(),
            }
    return rules


def parse_variables():
    """build/*.mk の `NAME := …` (行継続込み) を {NAME: [語…]} で返す。"""
    return check_select.read_makefiles()[1]


# ---------------------------------------------------------------------------
#  実行スクリプトとハーネスから対象ソースを拾う
# ---------------------------------------------------------------------------

PY_IN_CMD = re.compile(r"((?:tools|userland|sdk)/[A-Za-z0-9_./-]+\.py)")
C_IN_CMD = re.compile(r"((?:tools|drivers|kernel|fs|lib|net|exec|gfx)/[A-Za-z0-9_./-]+\.c)")
MANIFEST = re.compile(r"--manifest-path\s+(\S+)")


def literal_paths(node):
    """ROOT / "x" / "y" や素の文字列から repo 相対パスを取り出す"""
    out = []
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        if "/" in node.value and "." in node.value.rsplit("/", 1)[-1]:
            out.append(node.value)
    elif isinstance(node, ast.BinOp) and isinstance(node.op, ast.Div):
        parts = []
        cur = node
        while isinstance(cur, ast.BinOp) and isinstance(cur.op, ast.Div):
            if isinstance(cur.right, ast.Constant) and isinstance(cur.right.value, str):
                parts.append(cur.right.value)
            cur = cur.left
        if isinstance(cur, ast.Name) and cur.id in ("ROOT", "REPO", "PROJ"):
            parts.reverse()
            out.append("/".join(parts))
    elif isinstance(node, (ast.List, ast.Tuple)):
        for e in node.elts:
            out.extend(literal_paths(e))
    return out


def scan_python(relpath):
    """実行スクリプトから (ハーネス / 対象ソース候補, 記録の一覧) を静的に拾う"""
    text = read(relpath)
    if text is None:
        return [], []
    paths = []
    try:
        tree = ast.parse(text)
    except SyntaxError:
        tree = None
    if tree is not None:
        for node in tree.body:
            if not isinstance(node, ast.Assign):
                continue
            names = [t.id for t in node.targets if isinstance(t, ast.Name)]
            if not any(n in SRC_VARS or "SRC" in n for n in names):
                continue
            paths.extend(literal_paths(node.value))
    for p in ROOT_PATH.findall(text):
        if p not in paths:
            paths.append(p)
    head = "\n".join(text.split("\n")[:40])
    tdds = []
    for t in TDD_PATH.findall(head):
        if t not in tdds:
            tdds.append(t)
    return paths, tdds


INCLUDE_REAL = re.compile(r'^\s*#\s*include\s+"((?:\.\./)+)([A-Za-z0-9_./-]+)"', re.M)


def scan_harness_c(relpath):
    """ハーネス C が #include している「実物のソース」"""
    text = read(relpath)
    if text is None:
        return []
    out = []
    for _, path in INCLUDE_REAL.findall(text):
        if path.endswith((".c", ".inc", ".rs", ".asm")):
            out.append(path)
    return out


DISCOVER = re.compile(r"unittest\s+discover\s+-s\s+(\S+)\s+-p\s+'([^']+)'")


def runners_of(cmd):
    """コマンド 1 行が実際に走らせる Python スクリプトを列挙する"""
    out = []
    m = DISCOVER.search(cmd)
    if m:
        base = os.path.join(ROOT, m.group(1))
        for p in sorted(glob.glob(os.path.join(base, m.group(2)))):
            out.append(rel(p))
    for py in PY_IN_CMD.findall(cmd):
        if os.path.isfile(os.path.join(ROOT, py)) and py not in out:
            out.append(py)
    return out


def collect(target, rules):
    """ターゲット 1 つぶんの (コマンド, 対象ソース, 記録の一覧) を集める"""
    info = rules.get(target)
    if info is None:
        return [], [], []
    cmds = []
    sources = []
    tdds = []

    for cmd in info["recipe"]:
        shown = cmd.strip()
        if shown.startswith("@"):
            shown = shown[1:]
        cmds.append(shown)

        for py in runners_of(cmd):
            paths, found = scan_python(py)
            for t in found:
                if t not in tdds:
                    tdds.append(t)
            for p in paths:
                if p.startswith("tools/tests/") and p.endswith((".c", ".inc")):
                    sources.extend(scan_harness_c(p))
                elif p.endswith((".c", ".inc", ".rs", ".asm", ".py", ".mk")):
                    sources.append(p)
            # ハーネスが語幹一致で見つかる場合 (SRC 変数を持たない試験)
            stem = os.path.basename(py)
            if stem.startswith("test_"):
                cand = "tools/tests/%s_host.c" % stem[5:-3]
                if os.path.isfile(os.path.join(ROOT, cand)):
                    sources.extend(scan_harness_c(cand))

        for c in C_IN_CMD.findall(cmd):
            if c.startswith("tools/tests/"):
                sources.extend(scan_harness_c(c))
            else:
                sources.append(c)

        for man in MANIFEST.findall(cmd):
            sources.append(man)

    # 規則 2: Makefile のコメント塊
    if info["comment"]:
        for name in TDD_ANY.findall(info["comment"]):
            cand = "tools/tests/%s_tdd.md" % name
            if os.path.isfile(os.path.join(ROOT, cand)) and cand not in tdds:
                tdds.append(cand)

    # 規則 3: 語幹の一致
    stems = []
    for cmd in info["recipe"]:
        for py in runners_of(cmd):
            base = os.path.basename(py)[:-3]
            stems.append(base[5:] if base.startswith("test_") else base)
        for c in C_IN_CMD.findall(cmd):
            base = os.path.basename(c)[:-2]
            if base.endswith("_host"):
                base = base[:-5]
            stems.append(base)
    if target.startswith("check-"):
        stem = target[6:].replace("-", "_")
        stems.append(stem)
        if stem.endswith("_host"):
            stems.append(stem[:-5])
    for stem in stems:
        cand = "tools/tests/%s_tdd.md" % stem
        if os.path.isfile(os.path.join(ROOT, cand)) and cand not in tdds:
            tdds.append(cand)

    tdds = [t for t in tdds if os.path.isfile(os.path.join(ROOT, t))]

    # 重複を消しつつ順序は保つ
    uniq = []
    for s in sources:
        if s not in uniq and os.path.exists(os.path.join(ROOT, s)):
            uniq.append(s)
    return cmds, uniq, tdds


# ---------------------------------------------------------------------------
#  票 (規則 4)
# ---------------------------------------------------------------------------

def ticket_of(tdd_rel):
    """`*_tdd.md` の冒頭から票を拾う (規則 4)

    優先順は docs/**.md へのリンク > バッククォートの中の .md > 「票 B8」の記号。
    見出し (`# 票 H3 — …`) が先に来ても、本文の行に docs へのリンクがあれば
    そちらを採る。
    """
    text = read(tdd_rel)
    if text is None:
        return ""
    head = text.split("\n")[:16]
    blobs = [line + " " + nxt
             for line, nxt in zip(head, head[1:] + [""]) if "票" in line]
    for blob in blobs:
        m = DOC_LINK.search(blob)
        if m:
            return m.group(1)
    for blob in blobs:
        m = re.search(r"`([^`]+\.md)`", blob)
        if m:
            return m.group(1)
    for blob in blobs:
        m = re.search(r"票\s*([A-Za-z][A-Za-z0-9-]*)", blob)
        if m:
            return m.group(1)
    return ""


# ---------------------------------------------------------------------------
#  CI 被覆
# ---------------------------------------------------------------------------

def ci_targets(rules, targets):
    text = read(CI_WORKFLOW)
    if text is None:
        return set()
    covered = set()
    for t in targets:
        if re.search(r"\bmake\s+%s\b" % re.escape(t), text):
            covered.add(t)
            continue
        info = rules.get(t)
        if not info:
            continue
        for cmd in info["recipe"]:
            c = cmd.strip().lstrip("@").strip()
            if not c or c.startswith("#"):
                continue
            core = re.sub(r"^python3\s+(-B\s+)?", "", c).strip()
            if core and core in text:
                covered.add(t)
                break
    return covered


# ---------------------------------------------------------------------------
#  表の組み立て
# ---------------------------------------------------------------------------

def cell(items, kind="code"):
    if not items:
        return "—"
    if kind == "code":
        return "<br>".join("`%s`" % i for i in items)
    return "<br>".join(items)


def link_cell(path):
    if not path:
        return "—"
    if not os.path.isfile(os.path.join(ROOT, path)):
        return "`%s`" % path
    href = os.path.relpath(os.path.join(ROOT, path),
                           os.path.dirname(os.path.join(ROOT, OUT_REL)))
    return "[`%s`](%s)" % (path, href.replace(os.sep, "/"))


def links_cell(paths):
    if not paths:
        return "—"
    return "<br>".join(link_cell(p) for p in paths)


def table(rows):
    out = ["| # | ターゲット | コマンド | 対象ソース | 記録 (RED→GREEN) | 票 | CI |",
           "|---|---|---|---|---|---|---|"]
    for n, r in enumerate(rows, 1):
        out.append("| %d | `%s` | %s | %s | %s | %s | %s |" % (
            n, r["target"], cell(r["cmds"]), cell(r["sources"]),
            links_cell(r["tdds"]), links_cell(r["tickets"]),
            "○" if r["ci"] else "×"))
    return "\n".join(out)


def build_rows(rules, names, ci):
    rows = []
    for t in names:
        cmds, sources, tdds = collect(t, rules)
        tickets = []
        for tdd in tdds:
            tk = ticket_of(tdd)
            if tk and tk not in tickets:
                tickets.append(tk)
        rows.append({
            "target": t,
            "cmds": cmds,
            "sources": sources,
            "tdds": tdds,
            "tickets": tickets,
            "ci": t in ci,
        })
    return rows


# ---------------------------------------------------------------------------
#  生成本文
# ---------------------------------------------------------------------------
HEADER = """# 試験一覧 (生成物)

`make tests-inventory` で build/*.mk と試験スクリプトから生成する。コミットしない。
読み方・判断・手書きの節は [docs/TESTS.md](../../docs/TESTS.md)。
"""

RULE_TEXT = """## 1. 名前の対応規則

表の「記録」列 (`tools/tests/*_tdd.md`) は、ターゲット名から機械的に決める。3 つの規則
すべてを当てて、当たったものを**全部**並べる。どれも当たらなければ空欄 —
**記録が無いことも事実**なので埋めない。

| 規則 | 当て方 |
|---|---|
| 1 | 実行スクリプト冒頭 40 行 (docstring / コメント) が名指しする `tools/tests/<x>_tdd.md` |
| 2 | `build/*.mk` のターゲット直前のコメント塊が名指しする `<x>_tdd.md` |
| 3 | 語幹の一致 — `test_<stem>.py` / `<stem>_host.c` / `check-<stem>` に対する `tools/tests/<stem>_tdd.md` |
| 4 | 「票」列は、当たった `*_tdd.md` の冒頭 16 行の「票」の行から `docs/**.md` のリンク > バッククォートの `.md` > 「票 B8」の記号、の順で取る |

1 つのターゲットが複数のスクリプトを回すとき (`check-memory-host` `check-tools-host`
`check-vfs-mount-dev-host` など) は、当たった記録を**全部**並べる。

「対象ソース」列は 2 通りで拾う。(a) ハーネス C が `#include "../../…"` している
**出荷するソース**、(b) 実行スクリプトの module 直下の代入 (`SRC` / `HOST_SRC` /
`TARGET_SRCS` など) にある `ROOT / "…"`。どちらにも現れないものは空欄になる
(Rust は `--manifest-path` を代わりに出す)。

## 2. `make check` の列 ({n} ターゲット)

`build/checks.d/*.mk` の登録から集めた `CHECK_PAR_TARGETS` (`check-par` の依存) が
正典。この表はその列をそのまま展開したもの。コマンド列は `make check` (変異込み) の形。
"""


def render():
    rules = parse_makefiles()
    # `make check` が回すのは check-par の依存 (列は変数 CHECK_PAR_TARGETS)。
    # 2026-09-17〜26 は逐次の 2 段目 check-mut があった — 残っていれば数える。
    variables = parse_variables()
    names = []
    for stage in ("check-par", "check-mut"):
        for dep in (rules.get(stage) or {}).get("deps", []):
            m = re.match(r"^\$\((\w+)\)$", dep)
            names += variables.get(m.group(1), []) if m else [dep]
    # check-fast / check-changed は列を回す側 (recipe が $(MAKE)) なので数えない。
    all_check_targets = sorted(
        t for t in rules
        if t.startswith("check-") and rules[t]["recipe"]
        and not any("$(MAKE)" in c for c in rules[t]["recipe"]))
    outside = [t for t in all_check_targets if t not in names]

    ci = ci_targets(rules, names + outside)
    rows = build_rows(rules, names, ci)
    out_rows = build_rows(rules, outside, ci)

    parts = [HEADER, RULE_TEXT.replace("{n}", str(len(names))),
             table(rows), "",
             "## 3. `check` の列に入っていない `check-*` ターゲット (%d)"
             % len(outside), "",
             "門ではなく計測器・実機試験。`make check` からは呼ばれない。", "",
             table(out_rows), ""]
    return "\n".join(parts).rstrip() + "\n"


def check_registration():
    """本文の鮮度によらず、登録列・recipe・対応表の漏れを検出する。"""
    rules = parse_makefiles()
    names = check_select.check_lists(parse_variables())
    errors = []
    mapping = check_select.load_map()["checks"]
    for name in sorted(set(names) ^ set(mapping)):
        errors.append("登録列と入力対応表が一致しない: " + name)
    for name in names:
        if name not in rules or not rules[name]["recipe"]:
            errors.append("登録された検査に recipe が無い: " + name)
    for name, info in rules.items():
        if not name.startswith("check-"):
            continue
        for cmd in info["recipe"]:
            for script in PY_IN_CMD.findall(cmd):
                if not os.path.isfile(os.path.join(ROOT, script)):
                    errors.append("%s: 試験スクリプトが無い: %s" % (name, script))
                if name in mapping and not check_select.matches(
                        script, check_select.compile_globs(mapping[name])):
                    errors.append("%s: 試験スクリプトが入力対応表に無い: %s" % (name, script))
    for error in errors:
        print(error, file=sys.stderr)
    if not errors:
        print("試験の登録照合: %d ターゲット、漏れ 0 件" % len(names))
    return int(bool(errors))


def render_host32():
    names = []
    for info in parse_makefiles().values():
        for cmd in info["recipe"]:
            if "HOST32_RUNNERS ごと:" in cmd:
                names += [os.path.basename(p)[5:-3] for p in PY_IN_CMD.findall(cmd)]
    return ("# HOST32 試験一覧 (生成物)\n\n"
            "`make tests-inventory` で生成する。コミットしない。\n\n"
            "`HOST32_RUNNERS` は既定 `native qemu`。%d 試験 (%s) の正常対照を指定した全 runner、"
            "変異を先頭の runner で実行する。native への自動 fallback はしない。\n"
            % (len(names), " / ".join(names)))


def main():
    ap = argparse.ArgumentParser(description="試験一覧の生成と登録検査")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true", help="build/out/ に試験一覧を書く")
    mode.add_argument("--check", action="store_true", help="登録漏れを検査する (生成物は不要)")
    args = ap.parse_args()
    if args.check:
        return check_registration()
    fresh = render()
    if args.write:
        for relpath, text in ((OUT_REL, fresh), (HOST32_REL, render_host32())):
            path = os.path.join(ROOT, relpath)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            print("%s を書き出した" % relpath)
    else:
        sys.stdout.write(fresh)
    return 0


if __name__ == "__main__":
    sys.exit(main())
