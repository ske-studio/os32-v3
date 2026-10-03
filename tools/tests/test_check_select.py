"""make check-changed の選び方 (tools/check_select.py + tools/check_map.yaml) の試験。

代行レビュー (2026-09-26、P2-1〜P2-3) の筋書きを固定する:
  * 実装の .c が #include する .inc を変えたら、その .c を見る検査が変異込みになる
    (userland/system/hsync_protect.inc、userland/shell/sh_pipe.inc)
  * 裸のファイル名で文書を読む検査器 (README.md → check-kapi-version、
    CLAUDE.md → check-manifests) が docs だけの変更でも回る
  * docs だけの変更では文書を読む検査 (docs_always:) を常に回す
  * 走査型 (broad:) の `**` glob にしか当たらない変更は安全側 (全部変異込み)
  * サブモジュールの bump は check-manifests / check-packages-host だけ変異込み
  * feat/gui の上でコミットした後は HEAD~1 を基点にする (merge-base == HEAD で
    「変更なし」に退化しない)
  * 実物の対応表に漏れが無い (--lint = 0)

過剰な full を減らした改修 (2026-09-26) の筋書き:
  * `#include "x.h"` を -I の探索先 (試験スクリプトの "-I…" / .h を持つ
    ディレクトリ名の文字列) で解決する (os32api.h → sdk/include/os32/)
  * `notest:` だけに当たり、どの検査の glob にも当たらない変更 → 全部を変異なし
  * `notest:` の変更でも検査の glob (走査型の ** を含む) に当たればその検査は
    変異込み (glob が勝つ)。`except:` に書いたものは notest に数えない
  * notest の番人: 拾えた入力が notest に当たると --lint が落ちる

Makefile / build/*.mk の変更は **「新しい試験を足す形」だけを決まった型との完全一致で
絞る** (ユーザー決定 2026-10-01 — 自前の構文解析は独立レビューで 2 回、make -n の比較は
3 回目で P1 → Makefile の変更の安全性を一般に証明するのをやめた。make は呼ばない)。
一時の git リポジトリに小さな Makefile を置き、基点のコミットと作業中の版で plan() を回す:
  * 正側: 列 + 規則で新しい検査を足した → その 1 本。既存の検査の recipe に型どおりの行を
    足した → その 1 本。コメント・空行だけ → 何も足さない (全部を変異なし)
  * 負側 (全部): 独立レビュー (Codex astra) 3 回分の反例 — define の中の endif・タブ付き
    endef・`\\` 継続の次の endef、$(call eval)、MAKE の再定義、target-specific、計算名、?=、
    .SECONDEXPANSION、export 変数の値の変更、.ONESHELL の追加、recipe の `-` の削除、
    遅延前提の eval、$(MAKE) の行、$(shell) の行、config.mk の空白の変更。
    型の境界 — $(MUT) 以外の `$`、`;` `>` `|` `&` バッククォート、旗の文字種、末尾の空白、
    タブ以外の字下げ、列以外への名前の追加、既存名と同じ名前の新規則、列に足さない新規則、
    列に足したが規則が無い、既存の recipe の横取り、継続行の途中、列からの削除・重複、
    script が木に無い / 対応表に無い、追加・削除・改名された .mk、--files (基点なし)。
    独立レビュー (7745a75) の P1: `.PHONY:` は型に無い、足した非 recipe 行 (規則・
    コメント・空行) の直後に基点の recipe 行が来る配置は全部。P2: 列を同じファイル内で
    動かしても (末尾・途中へ) 全部。59234b5 の P1: 基点のコメント・空行越しの横取り
    (規則だけ / 規則 + 足した recipe / コメントだけ / 空行だけ) も全部。d5dbb6e の P1
    (基点の ifeq 越し) → ユーザー決定: 新規則はファイル末尾の塊だけ、既存の検査への行は
    recipe が規則行の直後から連続する tab 行だけのときだけ、コメント・空行は末尾の塊か
    基点の tab 行に接しない場所だけ (それぞれの条件を外す変異が RED)。15f335c の P1:
    継続行の後半 (`\t@echo \\` の次の `check-a:`) を持ち主にしない、LF だけで行を分ける
    (VT / CR で 2 行を 1 行にした変更を消さない)、制御文字を含む足した行は全部
  * 実物の Makefile を基点 = HEAD で比べると差が無い。実物の make ファイルを基点にして
    check-memory-host に型どおりの行を足すとその 1 本 (main の e241312 / f4989ee の形)
  * 逐次の 2 段目 (CHECK_MUT_TARGETS) は無い — 列は 1 本で全部並列

  python3 -B tools/tests/test_check_select.py            # 筋書き
  python3 -B tools/tests/test_check_select.py --mutate   # 否定側 (選び方を壊して RED か)

変異は check_select.py の**写しの文字列**に当てて exec するので、実物は書き換えない
(check-par で回せる)。変異は mutpar (OS32_MUT_JOBS、プロセス) で並列に回し、fixture の
一時リポジトリは基点の内容ごとに 1 回作って写す。
"""
import atexit
import contextlib
import io
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar  # noqa: E402  (tools/tests/mutpar.py、同じディレクトリ — 変異の並列)

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "tools/check_select.py"


def load(text=None):
    mod = types.ModuleType("check_select_under_test")
    mod.__file__ = str(SRC)
    code = compile(text if text is not None else SRC.read_text(encoding="utf-8"),
                   str(SRC), "exec")
    exec(code, mod.__dict__)
    return mod


def run_plan(cs, files):
    mode, st, mu, _ = cs.plan(files)
    return mode, set(st), set(mu)


def with_map(cs, edit):
    """対応表を読み込んだ後に edit(m) で書き換えた版を cs.load_map に差す。
    戻り値は元の load_map (呼び出し側が finally で戻す)。"""
    orig = cs.load_map
    m = orig()
    edit(m)
    cs.load_map = lambda: m
    return orig


def git(d, *args):
    subprocess.run(["git", "-C", d] + list(args), check=True,
                   capture_output=True, text=True)


# ------------------------------------------------------------------ 筋書き
def case_recipe_semicolon(cs):
    rules, _ = cs.read_makefiles()
    script = 'tools/tests/' + 'test_access_walk.py'
    rules['check-semicolon'] = [
        '@set -e; for runner in $(HOST32_RUNNERS); do '
        'python3 -B ' + script + '; done']
    inputs = cs.extract(rules, 'check-semicolon')
    assert script in inputs, inputs
    assert 'exec/' + 'access_walk.c' in inputs, inputs


def case_inc_extract(cs):
    rules, _ = cs.read_makefiles()
    for t in ("check-hsync-h2-host", "check-settings-protect-host"):
        assert "userland/system/hsync_protect.inc" in cs.extract(rules, t), t
    assert "userland/shell/sh_pipe.inc" in cs.extract(rules, "check-sh-status-host")


def case_hsync_protect(cs):
    mode, st, mu = run_plan(cs, ["userland/system/hsync_protect.inc"])
    assert mode == "sel", mode
    for t in ("check-hsync-h2-host", "check-hsync-h3-host", "check-h4-manifest-host",
              "check-settings-protect-host"):
        assert t in mu, (t, mu)
    assert mu <= st, "変異込みの検査が回す列に無い"


def case_sh_pipe(cs):
    mode, st, mu = run_plan(cs, ["userland/shell/sh_pipe.inc"])
    assert mode == "sel", mode
    assert {"check-sh-status-host", "check-sh-truncation-host"} <= mu, mu
    assert "check-serialfs-host" in st and "check-serialfs-host" not in mu, \
        "当たらない検査は変異なしで回す"


def case_bare_extract(cs):
    rules, _ = cs.read_makefiles()
    assert "README.md" in cs.extract(rules, "check-kapi-version")
    assert "CLAUDE.md" in cs.extract(rules, "check-manifests")


def case_readme(cs):
    mode, st, mu = run_plan(cs, ["README.md"])
    assert mode == "docs", mode
    assert "check-kapi-version" in st, st
    assert "check-serialfs-host" not in st, "docs だけなのに重い検査を回した"


def case_claude(cs):
    mode, st, mu = run_plan(cs, ["CLAUDE.md"])
    assert mode == "docs", mode
    assert {"check-manifests", "check-constraints"} <= st, st


def case_docs_always(cs):
    mode, st, mu = run_plan(cs, ["docs/08_build.md"])
    assert mode == "docs", mode
    for t in ("check-constraints", "check-kapi-version", "check-manifests",
              "check-packages-host", "check-tests-inventory"):
        assert t in st, (t, st)


def case_broad_only(cs):
    # drivers/dev.c は走査型 (arch-asm / le-access) の ** にしか当たらない。
    # 文字列を割ってあるのは、check-map の抽出がこの試験の入力と数えないため
    # (数えるとこの試験自身の glob に当たって筋書きが崩れる)。
    mode, st, mu = run_plan(cs, ["drivers/" + "dev.c"])
    assert mode == "full", mode


def case_submodule(cs):
    mode, st, mu = run_plan(cs, ["apps", "game"])
    assert mode == "sel", mode
    assert mu == {"check-manifests", "check-packages-host"}, mu


def case_nothing(cs):
    mode, st, mu = run_plan(cs, [])
    assert mode == "fast" and not mu, mode


def case_single_stage(cs):
    # 逐次の 2 段目は無い: full は列の全部を 1 段で変異込み。--files (基点なし) の
    # Makefile は make の比較ができないので全部。
    _, vars_ = cs.read_makefiles()
    par = cs.check_lists(vars_)
    assert "check-sh-status-host" in par and "check-kapi-layout-host" in par
    for f in ("Makefile", "build/sdk.mk"):
        mode, st, mu = run_plan(cs, [f])
        assert mode == "full" and st == set(par) and mu == set(par), (f, mode)


def case_inc_dir_extract(cs):
    # cat_linenum_host.c の #include "os32api.h" は取り込む側の場所にも ROOT にも
    # 無く、test_cat_linenum.py の "-I" + ROOT / "sdk/include/os32" で見つかる。
    rules, _ = cs.read_makefiles()
    got = cs.extract(rules, "check-cat-linenum-host")
    assert "sdk/include/os32/os32api.h" in got, sorted(got)[:20]
    assert "include/types.h" in got, "-I の先のヘッダが取り込むヘッダも辿る"


def case_notest_fast(cs):
    # notest だけに当たり、どの検査の glob にも当たらない → 全部を変異なし
    f = "sample/" + "x.bin"
    orig = with_map(cs, lambda m: m["notest"].append("sample/**"))
    try:
        mode, st, mu = run_plan(cs, [f])
        assert mode == "fast" and not mu, (mode, mu)
        _, vars_ = cs.read_makefiles()
        assert st == set(cs.check_lists(vars_)), "fast は全部を回す"
        # notest の外 (表に無い) が混ざれば安全側
        mode, st, mu = run_plan(cs, [f, "tools/" + "no_such_tool.py"])
        assert mode == "full", mode
    finally:
        cs.load_map = orig


def case_notest_glob_wins(cs):
    # 外部コマンドは notest だが、userland/** を舐める走査型の検査は変異込み
    mode, st, mu = run_plan(cs, ["userland/cmds/" + "cal.c"])
    assert mode == "sel", mode
    assert "check-privileged" in mu, mu
    assert "check-serialfs-host" not in mu, mu
    # except: に書いた cfg.c は notest ではない (check-cfg-host が読む)
    assert not cs.is_notest("userland/cmds/" + "cfg.c",
                            cs.compile_notest(cs.load_map()["notest"]))
    # except を外して notest に入れても、検査の glob が勝つ
    def drop_except(m):
        m["notest"] = [e["glob"] if isinstance(e, dict) else e for e in m["notest"]]
    orig = with_map(cs, drop_except)
    try:
        mode, st, mu = run_plan(cs, ["userland/cmds/" + "cfg.c"])
        assert mode == "sel" and "check-cfg-host" in mu, (mode, mu)
    finally:
        cs.load_map = orig


def case_notest_guard(cs):
    # 試験が読むファイルを notest にすると --lint が落ちる (番人)
    orig = with_map(cs, lambda m: m["notest"].append("userland/system/" + "hsync.c"))
    err = io.StringIO()
    try:
        with contextlib.redirect_stderr(err), \
                contextlib.redirect_stdout(io.StringIO()):
            rc = cs.lint()
    finally:
        cs.load_map = orig
    assert rc != 0, "notest の番人が黙った"
    assert "notest なのに入力になっている" in err.getvalue(), err.getvalue()[:500]


def case_featgui_commit(cs):
    with tempfile.TemporaryDirectory(prefix="os32-cksel-") as d:
        git(d, "init", "-q", "-b", "feat/gui")
        git(d, "config", "user.email", "t@example.invalid")
        git(d, "config", "user.name", "t")
        # CI may enable maintenance globally. A background git process can
        # remove maintenance.lock while copytree copies this template.
        git(d, "config", "maintenance.auto", "false")
        git(d, "config", "gc.auto", "0")
        pathlib.Path(d, "a.txt").write_text("a\n")
        git(d, "add", "a.txt")
        git(d, "commit", "-q", "-m", "a")
        first = subprocess.run(["git", "-C", d, "rev-parse", "HEAD"],
                               capture_output=True, text=True).stdout.strip()
        pathlib.Path(d, "b.txt").write_text("b\n")
        git(d, "add", "b.txt")
        git(d, "commit", "-q", "-m", "b")
        old_root, old_trk = cs.ROOT, cs._TRACKED
        cs.ROOT, cs._TRACKED = d, None
        try:
            base, note = cs.default_base()
            assert base == first, (base, note)
            assert "HEAD~1" in note, note
            committed, work = cs.changed_files(base)
            assert committed == ["b.txt"], committed
            # 枝の上なら merge-base のまま
            git(d, "checkout", "-q", "-b", "wt/x")
            pathlib.Path(d, "c.txt").write_text("c\n")
            git(d, "add", "c.txt")
            git(d, "commit", "-q", "-m", "c")
            base2, note2 = cs.default_base()
            assert "merge-base" in note2 and "HEAD~1" not in note2, note2
            assert cs.changed_files(base2)[0] == ["c.txt"]
        finally:
            cs.ROOT, cs._TRACKED = old_root, old_trk


def case_lint_real(cs):
    err = io.StringIO()
    with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
        rc = cs.lint()
    assert rc == 0, err.getvalue()[:2000]


# ---------------------------------------------------------------- Makefile: 厳格な型の一致
# 一時の git リポジトリ。基点 (main) のコミットに置く小さな Makefile と build/sdk.mk。
# check-b は Makefile 側に置く (Makefile の変更も同じ型で絞れる)。check-z は列に無い規則、
# `# planned: check-x` は基点の字面に現れる名前 (新しい名前の重複の反例)。
FX_MAKEFILE = (
    "include build/sdk.mk\n"
    "all: gen.txt\n"
    "gen.txt:\n"
    "\techo old > $@\n"
    "# planned: check-x\n"
    ".PHONY: check-b\n"
    "check-b:\n"
    "\tpython3 -B tools/tests/test_b.py\n"
    "check-z:\n"
    "\tpython3 -B tools/tests/test_z.py\n"
)
FX_SDK = (
    "CHECK_PAR_TARGETS := check-a \\\n"
    "    check-b\n"
    "MUTATE ?= 1\n"
    "MUT = $(if $(filter 1,$(MUTATE)),--mutate)\n"
    "MUTS = $(if $(filter 1,$(MUTATE)),--mutants)\n"
    "export FOO = old\n"
    "check-a:\n"
    "\tpython3 -B tools/tests/test_a.py $(MUT)\n"
)
FX_CONFIG = "C_STD = -std=gnu11\n"
FX_SCRIPTS = ["tools/tests/test_%s.py" % n
              for n in ("a", "a2", "b", "b2", "c", "c2", "x", "z", "zz")]
# check-z は列に無いが対応表には glob がある (持ち主が列に無い検査の反例を対応表の検査で
# 隠さないため)
FX_MAP = dict(ignore=[], full=["Makefile", "build/*.mk", "sdk/kapi.json"],
              docs_only=["**/*.md"], notest=[], broad=[], docs_always=[],
              checks={"check-a": ["tools/tests/test_a*.py"],
                      "check-b": ["tools/tests/test_b*.py"],
                      "check-c": ["tools/tests/test_c*.py"],
                      "check-x": ["tools/tests/test_x*.py"],
                      "check-z": ["tools/tests/test_z*.py"]})
A_LINE = "\tpython3 -B tools/tests/test_a.py $(MUT)\n"
A2 = "\tpython3 -B tools/tests/test_a2.py $(MUT)\n"
B_LINE = "\tpython3 -B tools/tests/test_b.py\n"
B2 = "\tpython3 -B tools/tests/test_b2.py --quick $(MUTS)\n"
C_RULE = "check-c:\n\tpython3 -B tools/tests/test_c.py $(MUT)\n"
LIST_OLD = "    check-b\n"
LIST_NEW = "    check-b \\\n    check-c\n"


_TEMPLATES = {}          # 基点の内容 → 作った一時リポジトリ (git init + commit は内容ごとに 1 回)
_TEMPLATE_ROOT = None


def _template_for(base):
    """基点の内容が同じ fixture はリポジトリを 1 回だけ作り、以後は写しで済ます
    (約 90 本の反例で git init + commit が支配的だった — 2026-10-01 実測)。
    プロセスごとに別の置き場 (変異の並列はプロセス)。終了時に消す。"""
    global _TEMPLATE_ROOT
    key = tuple(sorted(base.items()))
    d = _TEMPLATES.get(key)
    if d is None:
        if _TEMPLATE_ROOT is None:
            _TEMPLATE_ROOT = tempfile.mkdtemp(prefix="os32-ckmk-tpl-")
            atexit.register(shutil.rmtree, _TEMPLATE_ROOT, True)
        d = tempfile.mkdtemp(prefix="t", dir=_TEMPLATE_ROOT)
        git(d, "init", "-q", "-b", "main")
        git(d, "config", "user.email", "t@example.invalid")
        git(d, "config", "user.name", "t")
        # CI may enable maintenance globally. A background git process can
        # remove maintenance.lock while copytree copies this template.
        git(d, "config", "maintenance.auto", "false")
        git(d, "config", "gc.auto", "0")
        for rel, text in base.items():
            p = pathlib.Path(d, rel)
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text)
        git(d, "add", ".")
        git(d, "commit", "-q", "-m", "base")
        _TEMPLATES[key] = d
    return d


class Fixture:
    """基点のコミット (main) を持つ一時リポジトリ (雛形の写し)。write() / edit() / append()
    は作業中の変更 (未コミット)。"""

    def __init__(self, cs, files=None):
        self.cs = cs
        base = {"Makefile": FX_MAKEFILE, "build/sdk.mk": FX_SDK, "build/config.mk": FX_CONFIG,
                "sdk/kapi.json": "{}", "gen.txt": "x\n"}
        base.update({s: "" for s in FX_SCRIPTS})
        base.update(files or {})
        self.d = tempfile.mkdtemp(prefix="os32-ckmk-")
        os.rmdir(self.d)
        shutil.copytree(_template_for(base), self.d, symlinks=True)

    def write(self, rel, text):
        p = pathlib.Path(self.d, rel)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)

    def edit(self, rel, old, new):
        text = pathlib.Path(self.d, rel).read_text()
        assert text.count(old) == 1, (rel, old)
        self.write(rel, text.replace(old, new))

    def append(self, rel, text):
        self.write(rel, pathlib.Path(self.d, rel).read_text() + text)

    def add_c(self, where="build/sdk.mk", rule=C_RULE, phony=""):
        """新しい検査 check-c: 列に足し、規則 (+ phony の行) を where の末尾に置く。"""
        self.edit("build/sdk.mk", LIST_OLD, LIST_NEW)
        self.append(where, phony + rule)

    def plan(self, base="main", files=None, map_=None):
        """(mode, [stage], [mut], 行)。files 無しなら git の変更 (基点...HEAD + 未コミット)。"""
        cs = self.cs
        saved = cs.ROOT, cs._TRACKED, cs.load_map
        m = dict(map_ or FX_MAP)
        cs.ROOT, cs._TRACKED, cs.load_map = self.d, None, lambda: m
        try:
            if files is None:
                committed, work = cs.changed_files(base)
                files = sorted(set(committed) | set(work))
            return cs.plan(files, base=base)
        finally:
            cs.ROOT, cs._TRACKED, cs.load_map = saved

    def close(self):
        shutil.rmtree(self.d, ignore_errors=True)


@contextlib.contextmanager
def fixture(cs, **kw):
    fx = Fixture(cs, **kw)
    try:
        yield fx
    finally:
        fx.close()


def _only(result, *names):
    mode, st, mu, lines = result
    assert mode == "sel" and set(mu) == set(names), (mode, mu, lines)
    assert any("型に一致" in l for l in lines), lines


def _fast(result, stage=None):
    mode, st, mu, lines = result
    assert mode == "fast" and not mu, (mode, mu, lines)
    if stage is not None:
        assert st == list(stage), (st, lines)


def _full(result, why=None):
    mode, st, mu, lines = result
    assert mode == "full" and mu == st, (mode, mu, lines)
    if why:
        assert any(why in l for l in lines), (why, lines)


def case_mk_new_check(cs):
    # (a)+(b): 列に足した新しい検査 1 本だけ。規則は sdk.mk でも Makefile でも、
    # 旗・$(MUTS)・2 行の recipe・途中のコメントと空行、どれも型の中 (.PHONY は書かない)
    with fixture(cs) as fx:
        fx.add_c()
        r = fx.plan()
        _only(r, "check-c")
        assert r[1] == ["check-a", "check-b", "check-c"], r[1]
    with fixture(cs) as fx:
        fx.add_c(where="Makefile")
        _only(fx.plan(), "check-c")
    with fixture(cs) as fx:
        fx.add_c(rule="check-c:\n\tpython3 -B tools/tests/test_c.py --quick --no-image $(MUTS)\n"
                      "\n# second\n\tpython3 -B tools/tests/test_c2.py\n")
        _only(fx.plan(), "check-c")
    with fixture(cs) as fx:
        # 列の同じ物理行に足す (継続の付け替えは字面でなく語の集合で見る)
        fx.edit("build/sdk.mk", LIST_OLD, "    check-b check-c\n")
        fx.append("build/sdk.mk", C_RULE)
        _only(fx.plan(), "check-c")


def case_mk_recipe_add(cs):
    # (c): 既存の検査の recipe に型どおりの行 (末尾・途中・空行やコメントの後) → その 1 本
    with fixture(cs) as fx:
        fx.append("build/sdk.mk", A2)
        _only(fx.plan(), "check-a")
        fx.edit("Makefile", "check-b:\n" + B_LINE, "check-b:\n" + B2 + B_LINE)
        _only(fx.plan(), "check-a", "check-b")
    with fixture(cs) as fx:
        # 列の位置はそのまま、列の手前と後ろにコメントを足すのは型の中
        fx.edit("build/sdk.mk", "CHECK_PAR_TARGETS :=", "# list\nCHECK_PAR_TARGETS :=")
        fx.edit("build/sdk.mk", LIST_OLD, LIST_OLD + "# after\n")
        fx.append("build/sdk.mk", A2)
        _only(fx.plan(), "check-a")


def case_mk_comment_only(cs):
    with fixture(cs) as fx:
        fx.write("build/sdk.mk", "# comment\n\n" + FX_SDK + "\n# tail\n")
        fx.edit("Makefile", "check-b:\n", "# before b\ncheck-b:\n")
        r = fx.plan()
        _fast(r, ["check-a", "check-b"])
        assert any("追加選択なし" in l for l in r[3]), r[3]


def case_mk_mixed(cs):
    # 他の変更ファイルの選択と合算。sdk/kapi.json は生成物を介するので全部のまま
    with fixture(cs) as fx:
        fx.append("build/sdk.mk", A2)
        fx.write("tools/tests/test_b.py", "changed\n")
        _only(fx.plan(), "check-a", "check-b")
        fx.write("sdk/kapi.json", '{"v": 2}')
        _only(fx.plan(), "check-a", "check-b")


def case_mk_git_versions(cs):
    # 分岐した基点は merge-base で比べる (main の先の変更は見ない)。コミット済み +
    # staged + 未 staged を全部見る。select() も基点を渡す
    with fixture(cs) as fx:
        d = fx.d
        git(d, "checkout", "-q", "-b", "work")
        fx.append("build/sdk.mk", A2)
        git(d, "add", ".")
        git(d, "commit", "-q", "-m", "recipe")
        git(d, "checkout", "-q", "main")
        fx.edit("Makefile", "echo old > $@", "echo diverged > $@")
        git(d, "add", ".")
        git(d, "commit", "-q", "-m", "diverged base")
        git(d, "checkout", "-q", "work")
        _only(fx.plan(), "check-a")
        cs_root, cs_trk, cs_map = cs.ROOT, cs._TRACKED, cs.load_map
        cs.ROOT, cs._TRACKED, cs.load_map = d, None, lambda: dict(FX_MAP)
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
                assert cs.select("main") == 0
        finally:
            cs.ROOT, cs._TRACKED, cs.load_map = cs_root, cs_trk, cs_map
        assert "CC_MODE=sel\n" in out.getvalue() and "CC_MUT1=check-a\n" in out.getvalue(), \
            out.getvalue()
        fx.edit("Makefile", "check-b:\n" + B_LINE, "check-b:\n" + B_LINE + B2)
        git(d, "add", ".")
        _only(fx.plan(), "check-a", "check-b")
        fx.edit("Makefile", "echo old > $@", "echo work > $@")
        _full(fx.plan(), "削除・変更行")


def _neg_cases():
    """全部に倒す反例。(名前, fixture の files, 作業中の変更 fn(fx), 理由の一部)。
    独立レビュー (Codex astra) 3 回分の反例と型の境界。"""
    D = "define BODY\nendif\ncheck-a:\nendef\n"
    TAB_ENDEF = "define BODY\n\tendef\ncheck-a:\nendef\n"
    BS_ENDEF = "define BODY\nfoo \\\nendef\ncheck-a:\nendef\n"
    out = []

    def neg(name, fn, files=None, why="型に合わない"):
        out.append((name, files, fn, why))

    # astra の反例
    neg("define 内 endif", lambda fx: fx.edit("build/sdk.mk", "endif\ncheck-a:\n", "endif\ncheck-a:\n" + A2),
        {"build/sdk.mk": FX_SDK + D}, "define / 条件の中")
    neg("タブ付き endef", lambda fx: fx.edit("build/sdk.mk", "\tendef\ncheck-a:\n", "\tendef\ncheck-a:\n" + A2),
        {"build/sdk.mk": FX_SDK + TAB_ENDEF}, "define / 条件の中")
    neg("継続の次の endef", lambda fx: fx.edit("build/sdk.mk", "endef\ncheck-a:\n", "endef\ncheck-a:\n" + A2),
        {"build/sdk.mk": FX_SDK + BS_ENDEF}, "define / 条件の中")
    neg("ifeq の中", lambda fx: fx.edit("build/sdk.mk", "check-a:\n" + A_LINE, "check-a:\n" + A_LINE + A2),
        {"build/sdk.mk": FX_SDK.replace("check-a:\n" + A_LINE, "ifeq (1,1)\ncheck-a:\n" + A_LINE + "endif\n")},
        "define / 条件の中")
    neg("$(call eval)", lambda fx: fx.append("build/sdk.mk", "\t@echo $(call eval,FLAGS := new)\n"))
    neg("MAKE の再定義", lambda fx: fx.append("build/sdk.mk", "MAKE := echo\n"))
    neg("target-specific", lambda fx: fx.append("build/sdk.mk", "check-a: V = new\n"))
    neg("計算名", lambda fx: fx.append("build/sdk.mk", "N := FLAGS\n$(N) = new\n"))
    neg("?= の追加", lambda fx: fx.append("build/sdk.mk", "FLAGS ?= new\n"))
    neg("?= の変更", lambda fx: fx.edit("build/sdk.mk", "MUTATE ?= 1", "MUTATE ?= 0"), None, "削除・変更行")
    neg(".SECONDEXPANSION", lambda fx: fx.append("build/sdk.mk", ".SECONDEXPANSION:\n"))
    neg("export の値の変更", lambda fx: fx.edit("build/sdk.mk", "export FOO = old", "export FOO = new"),
        None, "削除・変更行")
    neg("export の追加", lambda fx: fx.append("build/sdk.mk", "export BAR = new\n"))
    neg(".ONESHELL の追加", lambda fx: fx.append("build/sdk.mk", ".ONESHELL:\n"))
    neg(".ONESHELL の下で型の行", lambda fx: fx.append("build/sdk.mk", A2),
        {"build/sdk.mk": ".ONESHELL:\n" + FX_SDK}, ".ONESHELL")
    neg("recipe の - の削除", lambda fx: fx.edit("build/sdk.mk", "\t-python3", "\tpython3"),
        {"build/sdk.mk": FX_SDK.replace("\tpython3", "\t-python3")}, "削除・変更行")
    neg("遅延前提の eval", lambda fx: fx.add_c(rule="check-c: $$(call eval,FLAGS := new)\n"
                                                      "\tpython3 -B tools/tests/test_c.py\n"))
    neg("$(MAKE) の行", lambda fx: fx.append("build/sdk.mk", "\t$(MAKE) check-b\n"))
    neg("$(shell) の行", lambda fx: fx.append("build/sdk.mk", "\tpython3 -B tools/tests/test_a.py $(shell echo --x)\n"))
    neg("$(shell) の代入", lambda fx: fx.append("build/sdk.mk", "X := $(shell touch gen.txt)\n"))
    neg("config.mk の空白", lambda fx: fx.edit("build/config.mk", "C_STD = ", "C_STD            = "),
        None, "削除・変更行")
    neg("既存の行への $(MUT)", lambda fx: fx.edit("Makefile", B_LINE, "\tpython3 -B tools/tests/test_b.py $(MUT)\n"),
        None, "削除・変更行")
    neg("既存の行の変更 (ファイル末尾)", lambda fx: fx.edit("build/sdk.mk", A_LINE, "\tpython3 -B tools/tests/test_a.py --more $(MUT)\n"),
        None, "削除・変更行")
    # 型の境界
    for bad in ("\tpython3 -B tools/tests/test_a.py $(FOO)\n",
                "\tpython3 -B tools/tests/test_a.py $(MUT) $(MUT)\n",
                "\tpython3 -B tools/tests/test_a.py $(MUT) --after\n",
                "\tpython3 -B tools/tests/test_a.py; rm -rf x\n",
                "\tpython3 -B tools/tests/test_a.py > out\n",
                "\tpython3 -B tools/tests/test_a.py | cat\n",
                "\tpython3 -B tools/tests/test_a.py &\n",
                "\tpython3 -B tools/tests/test_a.py `id`\n",
                "\tpython3 -B tools/tests/test_a.py --Quick\n",
                "\tpython3 -B tools/tests/test_a.py --x1\n",
                "\tpython3 -B tools/tests/test_a.py --x_y\n",
                "\tpython3 -B tools/tests/test_a.py -x\n",
                "\tpython3 -B tools/tests/test_a.py \n",
                "\tpython3 tools/tests/test_a.py\n",
                "\tpython3 -B tools/tests/Test_A.py\n",
                "\tpython3 -B ../tools/tests/test_a.py\n",
                "  python3 -B tools/tests/test_a.py\n",
                "\t\tpython3 -B tools/tests/test_a.py\n",
                "\t@python3 -B tools/tests/test_a.py\n",
                "\tcargo test --manifest-path x/Cargo.toml\n",
                " \n",
                "# comment \\\n",
                "OTHER := check-c\n"):
        neg("境界 %r" % bad, lambda fx, bad=bad: fx.append("build/sdk.mk", bad))
    neg("規則の末尾の空白", lambda fx: fx.add_c(rule="check-c: \n\tpython3 -B tools/tests/test_c.py\n"))
    # 独立レビュー P1: .PHONY は型に無い。既存の規則と recipe の間に挟む配置は全部
    neg(".PHONY の追加 (末尾)", lambda fx: fx.add_c(phony=".PHONY: check-c\n"), None, ".PHONY")
    neg("既存の .PHONY 行への追記", lambda fx: (fx.add_c(),
                                               fx.edit("Makefile", ".PHONY: check-b", ".PHONY: check-b check-c")),
        None, "削除・変更行")
    neg(".PHONY だけ (列に無い)", lambda fx: fx.append("build/sdk.mk", ".PHONY: check-c\n"))
    neg(".PHONY を規則と recipe の間に (astra の反例)",
        lambda fx: (fx.edit("build/sdk.mk", LIST_OLD, LIST_NEW),
                    fx.edit("build/sdk.mk", "check-a:\n" + A_LINE, "check-a:\n.PHONY: check-c\n" + A_LINE),
                    fx.append("build/sdk.mk", C_RULE)))
    neg("コメントを規則と recipe の間に", lambda fx: fx.edit("build/sdk.mk", "check-a:\n" + A_LINE, "check-a:\n# c\n" + A_LINE),
        None, "接して")
    neg("空行を規則と recipe の間に", lambda fx: fx.edit("build/sdk.mk", "check-a:\n" + A_LINE, "check-a:\n\n" + A_LINE),
        None, "接して")
    neg("コメントを recipe と recipe の間に", lambda fx: fx.edit("Makefile", B_LINE + B2, B_LINE + "# c\n" + B2),
        {"Makefile": FX_MAKEFILE.replace("check-b:\n" + B_LINE, "check-b:\n" + B_LINE + B2)}, "接して")
    neg("新規則 + コメント + 空行の後に既存の recipe",
        lambda fx: (fx.edit("build/sdk.mk", LIST_OLD, LIST_NEW),
                    fx.edit("build/sdk.mk", "check-a:\n" + A_LINE, "check-a:\n" + C_RULE + "# c\n\n" + A_LINE)),
        None, "末尾の塊にない")
    # ユーザー決定 (3 回目): 持ち主が変わる配置を締め出す
    IFEQ = "ifeq (1,1)\n\ttest \"$@\" != check-a\nendif\n"
    neg("astra 3 回目: 基点の ifeq 越しの横取り (新規則を check-a: の直後に)",
        lambda fx: (fx.edit("build/sdk.mk", LIST_OLD, LIST_NEW),
                    fx.edit("build/sdk.mk", "check-a:\n" + IFEQ, "check-a:\n" + C_RULE + IFEQ)),
        {"build/sdk.mk": FX_SDK.replace("check-a:\n" + A_LINE, "check-a:\n" + IFEQ)}, "末尾の塊にない")
    neg("新規則を基点の行のあいだに (recipe つき)",
        lambda fx: (fx.edit("build/sdk.mk", LIST_OLD, LIST_NEW),
                    fx.edit("build/sdk.mk", "MUTATE ?= 1\n", C_RULE + "\nMUTATE ?= 1\n")),
        None, "末尾の塊にない")
    neg("新規則を Makefile の途中に", lambda fx: fx.add_c(where="Makefile", rule=""),
        {"Makefile": FX_MAKEFILE}, "列に足した名前")
    neg("recipe の途中に基点のコメントがある検査への追加 (末尾)",
        lambda fx: fx.append("build/sdk.mk", A2),
        {"build/sdk.mk": FX_SDK.replace("check-a:\n" + A_LINE, "check-a:\n# base\n\n" + A_LINE)}, "持ち主が基点の検査の規則の行でない")
    neg("recipe の途中に基点のコメントがある検査への追加 (先頭)",
        lambda fx: fx.edit("build/sdk.mk", "check-a:\n", "check-a:\n" + A2),
        {"build/sdk.mk": FX_SDK.replace("check-a:\n" + A_LINE, "check-a:\n" + A_LINE + "# mid\n" + A_LINE.replace("test_a", "test_a2"))},
        "越しに続いている")
    neg("recipe の連続の後ろに基点の ifeq + tab 行がある検査への追加",
        lambda fx: fx.edit("build/sdk.mk", A_LINE, A_LINE + A2),
        {"build/sdk.mk": FX_SDK + IFEQ}, "越しに続いている")
    # 独立レビュー 4 回目: 継続行の後半を規則行と誤認しない / LF 以外を行境界にしない
    CONT = "check-b:\n\t@echo \\\ncheck-a:\n\t@true\n"
    neg("継続行の後半 check-a: を持ち主にしない (astra 4 回目)",
        lambda fx: fx.edit("build/sdk.mk", "\t@true\n", "\t@true\n" + A2),
        {"build/sdk.mk": FX_SDK.replace("check-a:\n" + A_LINE, CONT)}, "継続行の途中")
    neg("持ち主の規則行が継続で終わる", lambda fx: fx.edit("build/sdk.mk", "\tdep\n", "\tdep\n" + A2),
        {"build/sdk.mk": FX_SDK.replace("check-a:\n" + A_LINE, "check-a: \\\n\tdep\n")}, "継続行の途中")
    neg("VT で 2 行を 1 行にする (astra 4 回目: splitlines なら差が消える)",
        lambda fx: fx.edit("build/sdk.mk", "check-a:\n" + A_LINE, "check-a:\x0b" + A_LINE), None, "削除・変更行")
    neg("CR で 2 行を 1 行にする", lambda fx: fx.edit("build/sdk.mk", "check-a:\n" + A_LINE, "check-a:\r" + A_LINE),
        None, "削除・変更行")
    neg("CR を含むコメント (末尾の塊)", lambda fx: fx.append("build/sdk.mk", "# c\r\n"), None, "制御文字")
    neg("VT を含む recipe 行", lambda fx: fx.append("build/sdk.mk", "\tpython3 -B tools/tests/test_a2.py\x0b$(MUT)\n"), None, "制御文字")
    neg("recipe の連続の後ろに基点の空行 + tab 行がある検査への追加",
        lambda fx: fx.edit("build/sdk.mk", A_LINE, A_LINE + A2),
        {"build/sdk.mk": FX_SDK + "\n\tpython3 -B tools/tests/test_a2.py\n"}, "越しに続いている")
    # 独立レビュー P1 の 2 回目: 基点のコメント・空行越しの横取り。基点の check-a: と recipe の
    # 間にコメント / 空行 / 複数行の混在がある版で、(A) 規則の行だけ、(B) 規則 + 足した recipe
    # を check-a: の直後に挟む → 全部 (新規則は末尾の塊だけ)。コメント・空行だけを基点の
    # 非 tab 行のあいだに挟むのは (3) で許す (make は無視する) — 上の「接して」の反例とは別
    for tag, gap in (("コメント", "# base comment\n"), ("空行", "\n"), ("混在", "# c1\n\n# c2\n\n")):
        base = {"build/sdk.mk": FX_SDK.replace("check-a:\n" + A_LINE, "check-a:\n" + gap + A_LINE)}
        neg("基点の%s越し: 規則の行だけ" % tag,
            lambda fx, gap=gap: (fx.edit("build/sdk.mk", LIST_OLD, LIST_NEW),
                                 fx.edit("build/sdk.mk", "check-a:\n" + gap, "check-a:\ncheck-c:\n" + gap)), base)
        neg("基点の%s越し: 規則 + 足した recipe" % tag,
            lambda fx, gap=gap: (fx.edit("build/sdk.mk", LIST_OLD, LIST_NEW),
                                 fx.edit("build/sdk.mk", "check-a:\n" + gap, "check-a:\n" + C_RULE + gap)), base)
    # 独立レビュー P2: 列の移動 (同じファイル内) は位置が変わるので全部
    neg("列を末尾へ", lambda fx: (fx.edit("build/sdk.mk", "CHECK_PAR_TARGETS := check-a \\\n    check-b\n", ""),
                              fx.append("build/sdk.mk", "CHECK_PAR_TARGETS := check-a \\\n    check-b\n")),
        None, "削除・変更行")
    neg("列を途中へ", lambda fx: (fx.edit("build/sdk.mk", "CHECK_PAR_TARGETS := check-a \\\n    check-b\n", ""),
                              fx.edit("build/sdk.mk", "MUTATE ?= 1\n", "MUTATE ?= 1\nCHECK_PAR_TARGETS := check-a check-b\n")),
        None, "削除・変更行")
    neg("列を末尾へ + 名前の追加", lambda fx: (fx.edit("build/sdk.mk", "CHECK_PAR_TARGETS := check-a \\\n    check-b\n", ""),
                                       fx.append("build/sdk.mk", "CHECK_PAR_TARGETS := check-a check-b check-c\n" + C_RULE)),
        None, "削除・変更行")
    neg("既存名と同じ名前の新規則", lambda fx: fx.append("build/sdk.mk", "check-a:\n" + A2))
    neg("列に足さない新規則", lambda fx: fx.append("build/sdk.mk", C_RULE))
    neg("列に足したが規則が無い", lambda fx: fx.edit("build/sdk.mk", LIST_OLD, LIST_NEW))
    neg("列と規則の名前が違う", lambda fx: (fx.edit("build/sdk.mk", LIST_OLD, LIST_NEW),
                                         fx.append("build/sdk.mk", "check-c2:\n\tpython3 -B tools/tests/test_c2.py\n")))
    neg("新規則が 2 つ", lambda fx: fx.add_c(rule=C_RULE + C_RULE))
    neg("基点の字面にある名前", lambda fx: (fx.edit("build/sdk.mk", LIST_OLD, "    check-b \\\n    check-x\n"),
                                       fx.append("build/sdk.mk", "check-x:\n\tpython3 -B tools/tests/test_x.py\n")),
        None, "既にある")
    neg("既存の recipe の横取り", lambda fx: (fx.edit("build/sdk.mk", LIST_OLD, LIST_NEW),
                                          fx.edit("build/sdk.mk", "check-a:\n" + A_LINE, "check-a:\n" + C_RULE + A_LINE)))
    neg("recipe の無い新規則", lambda fx: fx.add_c(rule="check-c:\n"))
    neg("検査でない規則への recipe", lambda fx: fx.edit("Makefile", "\techo old > $@\n", "\techo old > $@\n" + A2))
    neg(".PHONY の後の recipe", lambda fx: fx.edit("Makefile", ".PHONY: check-b\n", ".PHONY: check-b\n" + B2))
    neg("列に無い検査への recipe", lambda fx: fx.append("Makefile", "\tpython3 -B tools/tests/test_z.py\n"))
    neg("include の後の recipe", lambda fx: fx.edit("Makefile", "include build/sdk.mk\n", "include build/sdk.mk\n" + A2))
    neg("target-specific の後の recipe", lambda fx: fx.edit("build/sdk.mk", "check-a: V = old\n", "check-a: V = old\n" + A2),
        {"build/sdk.mk": FX_SDK.replace("check-a:\n", "check-a: V = old\ncheck-a:\n")})
    neg("継続行の途中 (recipe)", lambda fx: fx.edit("build/sdk.mk", "test_a.py \\\n", "test_a.py \\\n" + A2),
        {"build/sdk.mk": FX_SDK.replace(A_LINE, "\tpython3 -B tools/tests/test_a.py \\\n\t  $(MUT)\n")},
        "継続行の途中")
    neg("継続行の途中 (コメント)", lambda fx: fx.edit("build/sdk.mk", "test_a.py \\\n", "test_a.py \\\n# c\n"),
        {"build/sdk.mk": FX_SDK.replace(A_LINE, "\tpython3 -B tools/tests/test_a.py \\\n\t  $(MUT)\n")},
        "継続行の途中")
    neg("列の := を +=", lambda fx: fx.edit("build/sdk.mk", "CHECK_PAR_TARGETS :=", "CHECK_PAR_TARGETS +="))
    neg("列からの削除", lambda fx: fx.edit("build/sdk.mk", "check-a \\\n    check-b", "check-b"), None, "消えた")
    neg("列の重複", lambda fx: fx.edit("build/sdk.mk", LIST_OLD, "    check-b check-b\n"), None, "重複")
    neg("列への 2 つ目の代入", lambda fx: fx.append("build/sdk.mk", "CHECK_PAR_TARGETS += check-c\n"))
    neg("列を別ファイルへ", lambda fx: (fx.edit("build/sdk.mk", "CHECK_PAR_TARGETS := check-a \\\n    check-b\n", ""),
                                     fx.append("Makefile", "CHECK_PAR_TARGETS := check-a check-b\n")))
    neg("script が木に無い", lambda fx: fx.append("build/sdk.mk", "\tpython3 -B tools/tests/test_a9.py $(MUT)\n"),
        None, "木に無い")
    neg("script が対応表の glob に無い", lambda fx: fx.append("build/sdk.mk", "\tpython3 -B tools/tests/test_zz.py\n"),
        None, "glob に入っていない")
    neg("新しい検査が対応表に無い", lambda fx: fx.add_c(rule="check-c:\n\tpython3 -B tools/tests/test_zz.py\n"),
        None, "glob に入っていない")
    neg("追加した .mk", lambda fx: (fx.edit("build/sdk.mk", LIST_OLD, LIST_NEW), fx.write("build/extra.mk", C_RULE)),
        None, "追加・削除・改名")
    neg("削除した .mk", lambda fx: os.unlink(os.path.join(fx.d, "build/config.mk")), None, "追加・削除・改名")
    neg("改名した .mk", lambda fx: os.rename(os.path.join(fx.d, "build/config.mk"), os.path.join(fx.d, "build/cfg.mk")),
        None, "追加・削除・改名")
    return out


_QUIET = False           # 変異の中 (run_cases(quiet=True)) — 反例は最初の 1 件で打ち切る


def case_mk_negative(cs):
    # 全部に倒すか、選択器が断る (SystemExit — 列の字面が読めない `+=` など。make
    # check-changed が rc≠0 で止まる = 安全側) のどちらか
    bad = []
    for name, files, fn, why in _neg_cases():
        with fixture(cs, files=files) as fx:
            fn(fx)
            try:
                _full(fx.plan(), why)
            except SystemExit:
                pass
            except AssertionError as e:
                bad.append("%s: %s" % (name, str(e)[:300]))
                if _QUIET:
                    break
    assert not bad, "\n".join(bad)


def case_mk_no_base(cs):
    # --files (基点なし)、基点が無い → 全部
    with fixture(cs) as fx:
        fx.append("build/sdk.mk", A2)
        _full(fx.plan(base="no-such-ref", files=["build/sdk.mk"]), "基点")
        _full(fx.plan(files=["build/sdk.mk"], base=None), "基点版なし")


def case_mk_maintenance_disabled(cs):
    # A globally enabled maintenance must not outlive the template commit.
    # Fresh base avoids the template cache, so this tests initialization too.
    from unittest.mock import patch
    with tempfile.TemporaryDirectory(prefix="os32-ckgit-") as d:
        config = pathlib.Path(d, "gitconfig")
        config.write_text("[maintenance]\n\tauto = true\n[gc]\n\tauto = 1\n")
        trace = pathlib.Path(d, "trace.jsonl")
        with patch.dict(os.environ, {"GIT_CONFIG_GLOBAL": str(config),
                                     "GIT_TRACE2_EVENT": str(trace)}):
            with fixture(cs, files={"maintenance-test.txt": "fresh\n"}) as fx:
                for name, want in (("maintenance.auto", "false"), ("gc.auto", "0")):
                    r = subprocess.run(["git", "-C", fx.d, "config", "--get", name],
                                       check=True, capture_output=True, text=True)
                    assert r.stdout.strip() == want, (name, r.stdout)
                assert not pathlib.Path(fx.d, ".git/objects/maintenance.lock").exists()
                events = [json.loads(line) for line in trace.read_text().splitlines()]
                assert any(e.get('event') == 'start' and 'commit' in e.get('argv', [])
                           for e in events), 'fixture commit missing from trace'
                children = [e.get('argv', []) for e in events
                            if e.get('event') == 'child_start']
                assert not any('maintenance' in argv or 'gc' in argv
                               for argv in children), children


def case_mk_real_tree(cs):
    # 実物の make ファイル (作業中の版) を基点に置いた一時リポジトリ: 列が読めて差が無い
    # (追加選択なし)。check-time-math-host に型どおりの行を足す (main の e241312 / f4989ee の形)
    # → その 1 本。tools/ は実物を指す (script の存在)。実物の木を HEAD と比べないのは、
    # 作業中に make ファイルを直しているあいだ (= check-changed の出番) にこの試験が落ちないため
    m = cs.load_map()
    files = {rel: pathlib.Path(cs.ROOT, rel).read_text(encoding="utf-8")
             for rel in cs.makefile_paths()}
    with fixture(cs, files=files) as fx:
        shutil.rmtree(os.path.join(fx.d, "tools"))
        os.symlink(os.path.join(cs.ROOT, "tools"), os.path.join(fx.d, "tools"))
        r = fx.plan(files=["build/sdk.mk", "Makefile"], map_=m)
        _fast(r)
        assert any("新しい検査 0 本" in l and "追加選択なし" in l for l in r[3]), r[3]
        rel = "build/checks.d/check-time-math-host.mk"
        text = pathlib.Path(fx.d, rel).read_text()
        fx.edit(rel, "\tpython3 -B tools/tests/test_time_math.py --target $(MUT)\n",
                "\tpython3 -B tools/tests/test_time_math.py --target $(MUT)\n"
                "\tpython3 -B tools/tests/test_time_math.py --target --again $(MUT)\n")
        r = fx.plan(files=[rel], map_=m)
        _only(r, "check-time-math-host", "check-map")
        assert len(r[1]) == len(cs.check_lists(cs.read_makefiles()[1])), r[1]



CASES = [case_recipe_semicolon, case_inc_extract, case_hsync_protect, case_sh_pipe, case_bare_extract,
         case_readme, case_claude, case_docs_always, case_broad_only,
         case_submodule, case_nothing, case_single_stage, case_inc_dir_extract,
         case_notest_fast, case_notest_glob_wins, case_notest_guard,
         case_featgui_commit,
         case_mk_new_check, case_mk_recipe_add, case_mk_comment_only, case_mk_mixed,
         case_mk_git_versions, case_mk_negative, case_mk_no_base, case_mk_real_tree, case_mk_maintenance_disabled]


def run_cases(cs, quiet=False, cases=None):
    global _QUIET
    _QUIET = quiet
    failed = []
    for c in CASES if cases is None else cases:
        try:
            c(cs)
            ok = True
        except (Exception, SystemExit) as e:   # noqa: BLE001 — 変異で何が起きても RED
            ok = False
            if not quiet:
                print("FAIL %s: %r" % (c.__name__, e), flush=True)
        if not ok:
            failed.append(c.__name__)
            if quiet:
                return failed            # 変異は最初に落ちたケースで打ち切る
        elif not quiet:
            print("ok   %s" % c.__name__, flush=True)
    return failed


# ------------------------------------------------------------------ 否定側
GLOB_MUTATIONS = [
    ('            self.pending.append(rel)\n', '            pass\n',
     "実装の .c / .inc の #include を辿らない (P2-1: .inc が表から落ちる)"),
    ('if "/" in s or s.endswith(BARE_EXTS):', 'if "/" in s:',
     "裸のファイル名 (README.md / CLAUDE.md) を候補にしない (P2-2)"),
    ('if not (t in broad and "**" in g)]', ']',
     "走査型の ** glob を「表に載っている」に数える (P2-1 の保険が外れる)"),
    ('run = hit | (set(m["docs_always"]) & set(par))', 'run = hit',
     "docs だけの変更で文書を読む検査を常には回さない (P2-2)"),
    ('    if branch in branches:\n', '    if False:\n',
     "feat/gui の上でコミットした後も merge-base (== HEAD) を基点にする (P2-3)"),
    ('    elif full_hits or unmatched:\n', '    elif full_hits:\n',
     "表に無い変更でも安全側 (全部変異込み) に倒さない"),
    ('        mu = [t for t in par if t in hit]\n',
     '        mu = [t for t in par if t in hit][:1]\n',
     "当たった検査の一部しか変異込みで回さない"),
    ('                for base in sorted(self.inc_dirs | {""}):',
     '                for base in [""]:',
     "#include を -I の探索先で解決しない (ヘッダが表から落ちる)"),
    ('            if is_notest(f, notest):\n                errs.append(',
     '            if False:\n                errs.append(',
     "notest の番人を外す (試験が読むファイルを notest にしても黙る)"),
    ('    elif not hit:\n        mode, st, mu = "fast", list(par), []',
     '    elif not hit:\n        mode, st, mu = "fast", [], []',
     "notest だけの変更で検査を 1 本も回さない"),
    ('    for f in changed:\n        hit |= {t for t, cg in checks.items() if matches(f, cg)}',
     '    for f in changed:\n        hit |= {t for t, cg in checks.items() if matches(f, cg)'
     ' and not is_notest(f, notest)}',
     "notest の変更は検査の glob に当たっても数えない (glob が勝たない)"),
    ('    return any(r.match(path) and not any(x.match(path) for x in ex)',
     '    return any(r.match(path)',
     "notest の except: を無視する"),
    ('    for f in changed:\n        hit |=', '    for f in changed[:0]:\n        hit |=',
     "変更を検査に突き合わせない"),
]
# Makefile の型の一致 (2026-10-01)。どれも make の筋書き (case_mk_*) で RED になる
MK_MUTATIONS = [
    ('        if j == len(ws):\n            return None', '        if j == len(ws):\n            break',
     "削除・変更行を見ない (基点の行が作業中に無くても追加だけと見なす)"),
    ('((?: --[a-z][a-z-]*)*)', '(.*)',
     "recipe の型を緩める (旗の後ろに何でも許す — `;` `>` `$(FOO)`)"),
    ('( \\$\\((?:MUT|MUTS)\\))?$', '( \\$\\(\\w+\\))?$',
     "$(MUT) / $(MUTS) 以外の $(...) を許す"),
    ('                if any(rx.search(l) for l in lines):', '                if False:',
     "新しい名前が基点の make ファイルに既にあっても見ない"),
    ('        if not base_words <= work_words:', '        if False:',
     "列の集合比較を外す (列から消えた名前を見ない)"),
    ('    if len(words) != len(set(words)):', '    if False:',
     "列の重複を見ない"),
    ('        if not ok[k]:', '        if False:',
     "define / 条件の深さを見ない"),
    ('        if not text.startswith("\\t"):', '        if False:',
     "make の読み方 (tab 行は endef にならない) を外して字下げを無視する読みだけにする"),
    ('        if not single_inserted(k):', '        if False:',
     "継続行の途中への挿入を見ない"),
    ('                if name not in base_words:\n                    raise Reject("%s: 持ち主 %s が基点の列にない"',
     '                if False:\n                    raise Reject("%s: 持ち主 %s が基点の列にない"',
     "持ち主が基点の列にある検査でなくても recipe の追加と見なす"),
    ('                if j < 0 or j in ins or not mo or "=" in t:', '                if j < 0 or j in ins or not mo:',
     "target-specific 変数の行 (check-a: V = …) を持ち主にできる"),
    ('                if (j > 0 and continued(work[j - 1])) or continued(t):', '                if False:',
     "継続行の後半 (`\\t@echo \\\\` の次の check-a:) を持ち主の規則行と誤認する (4 回目 P1)"),
    ('    return text.split("\\n")', '    return text.splitlines()',
     "LF 以外 (VT / FF / CR …) も行境界にして実際の行の変更を消す (4 回目 P1)"),
    ('        if CTRL_RE.search(line):', '        if False:',
     "制御文字 (CR など) を含む足した行を許す"),
    ('            if not os.path.isfile(os.path.join(ROOT, script)):', '            if False:',
     "script が木に無くても選ぶ"),
    ('            if not matches(script, m_checks.get(name, [])):', '            if False:',
     "script が対応表の当該検査の glob に無くても選ぶ"),
    ('        if not picked.get(name):', '        if False:',
     "recipe の無い新規則を許す"),
    ('            if not in_tail:\n                raise Reject("%s: 新しい規則', '            if False:\n                raise Reject("%s: 新しい規則',
     "(1) 新しい規則をファイル末尾の塊に限らない (基点の ifeq / コメント越しの横取り)"),
    ('                while j >= 0 and work[j].startswith("\\t"):\n                    j -= 1',
     '                while j >= 0 and (work[j].startswith("\\t") or _is_skippable(work[j])):\n                    j -= 1',
     "(2) 持ち主へ辿るときに基点のコメント・空行を飛ばす (recipe が連続でなくても足せる)"),
    ('                if j < len(work) and work[j].startswith("\\t"):\n                    raise Reject("%s: %s の recipe が',
     '                if False:\n                    raise Reject("%s: %s の recipe が',
     "(2) recipe の連続の後ろにディレクティブ・空行越しの tab 行が続いても見ない"),
    ('                if (p is not None and work[p].startswith("\\t")) or \\\n                        (n is not None and work[n].startswith("\\t")):',
     '                if False:',
     "(3) 基点の tab 行に接するコメント・空行を許す"),
    ('                out.append((None, LIST_MARKER))', '                pass',
     "列の物理行を除くだけで位置を保たない (P2: 同じファイル内の列の移動が追加だけに見える)"),
    ('        if set(headers) != new_names or any(n != 1 for n in headers.values()):',
     '        if False:',
     "列に足した名前と新しい規則の集合を突き合わせない"),
    ('            if any(".ONESHELL" in l for l in lines):', '            if False:',
     ".ONESHELL の下でも絞る"),
    ('            if rel not in bt or rel not in wt:', '            if False:',
     "追加・削除・改名されたファイルを見ない"),
    ('    ancestor = p.stdout.strip()\n    if p.returncode != 0 or not ancestor:',
     '    ancestor = base\n    if p.returncode != 0 or not ancestor:',
     "分岐した基点と直接比較する (merge-base でない)"),
    ('    if base is None:\n        return None, "基点版なし (--files)"', '    if False:\n        return None, ""',
     "--files (基点なし) を全部に倒さない"),
    ('    return set(picked), why', '    return new_names, why',
     "recipe に行を足した既存の検査を選ばない"),
    ('    return set(picked), why', '    return set(picked) - new_names, why',
     "列に足した新しい検査を選ばない"),
    ('        if f == "sdk/kapi.json":', '        if False:',
     "KAPI generated readers routing disabled"),
    ('base=None if files is not None else base)', 'base=None)',
     "select から基点を渡さず絞り込めない"),
]
MUTATIONS = GLOB_MUTATIONS + MK_MUTATIONS


def _run_mutation(i):
    """変異 i (1 始まり) を check_select.py の写しの文字列に当てて筋書きを回す。
    (i, 状態, 最初に落ちたケース名または例外の repr)。mutpar の processes=True で
    プロセスごとに回す (fixture の一時リポジトリはプロセスごとに別、実物は読むだけ)。"""
    original = SRC.read_text(encoding="utf-8")
    old, new, why = MUTATIONS[i - 1]
    if original.count(old) != 1:
        return i, "NOT APPLICABLE", ""
    glob_cases = [c for c in CASES if not c.__name__.startswith("case_mk_")]
    mk_cases = [c for c in CASES if c.__name__.startswith("case_mk_")]
    try:
        cs = load(original.replace(old, new))
        # make の変異は fixture (速い) を先に、実物の木の lint は後ろに
        cases = mk_cases + glob_cases if i > len(GLOB_MUTATIONS) else glob_cases + mk_cases
        failed = run_cases(cs, quiet=True, cases=cases)
    except Exception as e:           # noqa: BLE001
        return i, "INVALID", repr(e)
    return i, ("RED" if failed else "GREEN"), (failed[0] if failed else "")


def mutation_maintenance_order():
    """Move fixture suppression past commit; trace must catch the child git."""
    import inspect
    from unittest.mock import patch
    text = inspect.getsource(_template_for)
    settings = ('        git(d, "config", "maintenance.auto", "false")\n'
                '        git(d, "config", "gc.auto", "0")\n')
    commit = '        git(d, "commit", "-q", "-m", "base")\n'
    assert text.count(settings) == text.count(commit) == 1
    text = text.replace(settings, '').replace(commit, commit + settings)
    namespace = dict(globals(), _TEMPLATES={}, _TEMPLATE_ROOT=None)
    exec(compile(text, __file__, 'exec'), namespace)
    with patch.dict(globals(), {'_template_for': namespace['_template_for']}):
        try:
            case_mk_maintenance_disabled(load())
        except AssertionError as error:
            assert 'maintenance' in str(error) or "'gc'" in str(error), error
            print('MUTATION maintenance config after commit RED (trace child_start)', flush=True)
            return 0
    print('MUTATION maintenance config after commit **GREEN (見逃し)**', flush=True)
    return 1


def mutate():
    """変異を並列に回す (mutpar、OS32_MUT_JOBS)。結果は変異の番号順に出す。"""
    bad = mutation_maintenance_order()
    for i, status, info in mutpar.run_ordered(_run_mutation, range(1, len(MUTATIONS) + 1),
                                              processes=True):
        why = MUTATIONS[i - 1][2]
        if status == "RED":
            print("MUTATION %d RED (%s): %s" % (i, info, why), flush=True)
        elif status == "GREEN":
            print("MUTATION %d **GREEN (見逃し)**: %s" % (i, why), flush=True)
            bad += 1
        else:
            print("MUTATION %d %s%s: %s" % (i, status, " (load): " + info if info else "", why), flush=True)
            bad += 1
    return bad


if __name__ == "__main__":
    os.chdir(ROOT)
    failed = run_cases(load())
    print("SUMMARY %d/%d PASS" % (len(CASES) - len(failed), len(CASES)), flush=True)
    rc = len(failed)
    if "--mutate" in sys.argv[1:]:
        rc += mutate()
    sys.exit(bool(rc))
