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
MAKEFILES = ["Makefile"] + sorted(
    os.path.join("build", f) for f in os.listdir(os.path.join(ROOT, "build"))
    if f.endswith(".mk"))


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
def read_makefiles():
    """{検査名: [recipe 行]} と {変数名: 値} を返す (check-* の規則だけ)。"""
    rules, vars_ = {}, {}
    for mf in MAKEFILES:
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


def plan(files):
    """変更の一覧から (mode, stage, mut, 説明の行) を決める。git は見ない。

    stage は回す検査 (make の目標)、mut はそのうち変異込みで回す検査。"""
    m = load_map()
    _, vars_ = read_makefiles()
    par = check_lists(vars_)
    ign = compile_globs(m["ignore"])
    full = compile_globs(m["full"])
    docs = compile_globs(m["docs_only"])
    notest = compile_notest(m["notest"])
    broad = set(m["broad"])
    checks = {t: compile_globs(g) for t, g in m["checks"].items()}
    # 走査型の検査 (broad:) の `**` を含む glob は「表に載っている」の判定に数えない
    # — ツリーを丸ごと舐める検査に当たっただけで安全側が発火しなくなるのを防ぐ
    # (代行レビュー P2-1 の保険)。変異込みで回す検査を選ぶ方には数える。
    cover = {t: [(g, r) for g, r in cg if not (t in broad and "**" in g)]
             for t, cg in checks.items()}
    changed = [f for f in files if not matches(f, ign)]

    hit, unmatched, full_hits, notest_hits = set(), [], [], []
    for f in changed:
        hit |= {t for t, cg in checks.items() if matches(f, cg)}
        if matches(f, full):
            full_hits.append(f)
        elif is_notest(f, notest):
            notest_hits.append(f)
        elif not matches(f, docs) and \
                not any(matches(f, cg) for cg in cover.values()):
            unmatched.append(f)

    lines = []
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
        lines.append("→ どのホスト試験の入力でもない変更だけ: 全部を変異なしで回す "
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
    mode, st, mu, lines = plan(sorted(set(committed) | set(work)))
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
