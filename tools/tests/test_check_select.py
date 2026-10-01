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

Makefile / build/*.mk の変更は **make 自身に展開させて比べる** (ユーザー決定 2026-10-01、
自前の構文解析は独立レビューで 2 回 P1 → 撤去)。一時の git リポジトリに小さな
Makefile を置き、基点のコミットと作業中の版で plan() を回す:
  * recipe に 1 行足した / 列に 1 本足した → その 1 本だけ変異込み。列から消した検査は
    回さない。コメントだけ → 全部を変異なし。ビルド (all) の recipe が変わった → 全部
  * 独立レビュー (Codex astra) の反例: define の中の endif・タブ付き endef・バックスラッシュ継続の
    次の endef、$(call eval,…)、組み込み名 MAKE の再定義、target-specific 変数、
    計算名の代入、?= / 条件付き定義とコマンド行上書き (MAKEFLAGS)、別ファイルの
    .SECONDEXPANSION + $$(call eval,…) — 影響を受ける検査が変異込みになる (または全部)
  * 検査をまたぐ影響 (recipe の $(eval) が別の検査を変える) は順・逆順・単独の展開の
    不一致で全部に倒す。順だけ・逆順だけ・単独だけで見える形をそれぞれ置く
  * make が失敗した、基点が無い、検査の列の読みが合わない、--files (基点なし) → 全部
  * 実物の Makefile を基点 = HEAD で比べると差が無い (dry-run できる、列が一致する)
  * 逐次の 2 段目 (CHECK_MUT_TARGETS) は無い — 列は 1 本で全部並列

  python3 -B tools/tests/test_check_select.py            # 筋書き
  python3 -B tools/tests/test_check_select.py --mutate   # 否定側 (選び方を壊して RED か)

変異は check_select.py の**写しの文字列**に当てて exec するので、実物は書き換えない
(check-par で回せる)。
"""
import contextlib
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import types

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
    for f in ("Makefile", "build/sdk.mk", "sdk/kapi.json"):
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


# ---------------------------------------------------------------- Makefile: make の dry-run 比較
# 一時の git リポジトリ。基点 (main) のコミットに置く小さな Makefile と build/sdk.mk。
# check-b は Makefile 側に置く (Makefile の変更も同じ比較で絞れる)。$(abspath .) は
# 一時ディレクトリの番地の正規化、$(MUT) は MUTATE=1 で展開させることの確認。
FX_MAKEFILE = (
    "include build/sdk.mk\n"
    "all: gen.txt\n"
    "gen.txt:\n"
    "\techo old > $@\n"
    ".PHONY: check-b\n"
    "check-b:\n"
    "\tpython3 b.py $(abspath .)\n"
)
FX_SDK = (
    "CHECK_PAR_TARGETS := check-a check-b\n"
    "MUTATE ?= 1\n"
    "MUT = $(if $(filter 1,$(MUTATE)),--mutate)\n"
    ".PHONY: check-a\n"
    "check-a:\n"
    "\tpython3 a.py $(MUT)\n"
)
FX_MAP = dict(ignore=[], full=["Makefile", "build/*.mk", "sdk/kapi.json"],
              docs_only=["**/*.md"], notest=[], broad=[], docs_always=[],
              checks={"check-a": ["src/a.py"], "check-b": ["src/b.py"]})


class Fixture:
    """基点のコミット (main) を持つ一時リポジトリ。write() は作業中の変更 (未コミット)。"""

    def __init__(self, cs, files=None, extra=None):
        self.cs = cs
        self.d = tempfile.mkdtemp(prefix="os32-ckmk-")
        base = {"Makefile": FX_MAKEFILE, "build/sdk.mk": FX_SDK, "src/a.py": "", "src/b.py": "",
                "sdk/kapi.json": "{}", "gen.txt": "x\n"}
        base.update(files or {})
        base.update(extra or {})
        git(self.d, "init", "-q", "-b", "main")
        git(self.d, "config", "user.email", "t@example.invalid")
        git(self.d, "config", "user.name", "t")
        for rel, text in base.items():
            self.write(rel, text)
        git(self.d, "add", ".")
        git(self.d, "commit", "-q", "-m", "base")

    def write(self, rel, text):
        p = pathlib.Path(self.d, rel)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)

    def edit(self, rel, old, new):
        text = pathlib.Path(self.d, rel).read_text()
        assert text.count(old) == 1, (rel, old)
        self.write(rel, text.replace(old, new))

    def plan(self, base="main", files=None):
        """(mode, [stage], [mut], 行)。files 無しなら git の変更 (基点...HEAD + 未コミット)。"""
        cs = self.cs
        saved = cs.ROOT, cs._TRACKED, cs.load_map
        cs.ROOT, cs._TRACKED, cs.load_map = self.d, None, lambda: dict(FX_MAP)
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


def _affected(result, *names):
    """影響を受ける検査が変異込みに選ばれる (または全部)。"""
    mode, st, mu, lines = result
    assert mode == "full" or set(names) <= set(mu), (mode, mu, lines)


def _only(result, *names):
    mode, st, mu, lines = result
    assert mode == "sel" and set(mu) == set(names), (mode, mu, lines)


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


def case_mk_recipe_line(cs):
    # recipe に 1 行足しただけ → その検査だけ。Makefile 側の検査も同じ
    with fixture(cs) as fx:
        fx.edit("build/sdk.mk", "\tpython3 a.py $(MUT)\n", "\tpython3 a.py $(MUT)\n\tpython3 a2.py\n")
        r = fx.plan()
        _only(r, "check-a")
        assert r[1] == ["check-a", "check-b"], r[1]
        assert any("絞り込み" in l for l in r[3]), r[3]
        fx.edit("Makefile", "python3 b.py", "python3 b.py --more")
        _only(fx.plan(), "check-a", "check-b")
    with fixture(cs) as fx:
        fx.edit("Makefile", "python3 b.py", "python3 b.py --more")
        _only(fx.plan(), "check-b")


def case_mk_add_remove(cs):
    with fixture(cs) as fx:
        fx.edit("build/sdk.mk", "check-a check-b\n", "check-a check-b check-c\n")
        fx.write("build/sdk.mk", pathlib.Path(fx.d, "build/sdk.mk").read_text() +
                 ".PHONY: check-c\ncheck-c:\n\tpython3 c.py\n")
        r = fx.plan()
        _only(r, "check-c")
        assert r[1] == ["check-a", "check-b", "check-c"], r[1]
    with fixture(cs) as fx:
        fx.edit("build/sdk.mk", "check-a check-b\n", "check-a\n")
        _fast(fx.plan(), ["check-a"])
        # 消した検査の規則も消す (make には無い目標)
        fx.edit("Makefile", ".PHONY: check-b\ncheck-b:\n\tpython3 b.py $(abspath .)\n", "")
        _fast(fx.plan(), ["check-a"])


def case_mk_comment_only(cs):
    with fixture(cs) as fx:
        fx.write("build/sdk.mk", "# comment\n" + FX_SDK)
        _fast(fx.plan(), ["check-a", "check-b"])


def case_mk_build_recipe(cs):
    # 検査が読む成果物の作り方 (all から辿れる recipe) が変わった → 全部
    with fixture(cs) as fx:
        fx.edit("Makefile", "echo old > $@", "echo new > $@")
        _full(fx.plan(), "ビルド")


def case_mk_prereq_file(cs):
    # 検査の前提のファイルが木にあっても (-B) その recipe の変更を見る
    sdk = FX_SDK.replace("check-a:\n", "check-a: stamp.txt\n") + "stamp.txt:\n\techo old > $@\n"
    with fixture(cs, files={"build/sdk.mk": sdk, "stamp.txt": "x\n"}) as fx:
        fx.edit("build/sdk.mk", "echo old", "echo new")
        _only(fx.plan(), "check-a")


def case_mk_mut_flag(cs):
    # 展開は MUTATE=1 で取る (変異ありの recipe を比べる)
    sdk = FX_SDK.replace("MUTATE ?= 1", "MUTATE ?= 0")
    with fixture(cs, files={"build/sdk.mk": sdk}) as fx:
        fx.edit("build/sdk.mk", "--mutate)", "--mutants)")
        _only(fx.plan(), "check-a")


def case_mk_define_state(cs):
    # 独立レビュー P1-1: define の中の endif / タブ付き endef / `\` 継続の次の endef は本文。
    # check-b が BODY を読むので old → new は check-b を変える。
    body = ("CHECK_PAR_TARGETS := check-b\n"
            "define BODY\nendif\ncheck-a:\n\techo old\nendef\n"
            ".PHONY: check-b\ncheck-b:\n\t@echo $(findstring old,$(BODY))\n")
    mk = FX_MAKEFILE.replace(".PHONY: check-b\ncheck-b:\n\tpython3 b.py $(abspath .)\n", "")
    for v in (body, body.replace("endif\n", "\tendef\n"),
              body.replace("endif\n", "foo \\\nendef\n")):
        with fixture(cs, files={"Makefile": mk, "build/sdk.mk": v}) as fx:
            fx.edit("build/sdk.mk", "echo old", "echo new")
            _affected(fx.plan(), "check-b")


def case_mk_call_eval(cs):
    # 独立レビュー P1-2: $(call eval,…) が別の検査の変数を変える → 順序依存 → 全部
    sdk = FX_SDK.replace("\tpython3 a.py $(MUT)\n", "\t@echo a$(call eval,FLAGS := old)\n")
    mk = FX_MAKEFILE.replace("python3 b.py $(abspath .)", "@echo $(FLAGS)")
    with fixture(cs, files={"Makefile": mk, "build/sdk.mk": sdk}) as fx:
        fx.edit("build/sdk.mk", "FLAGS := old", "FLAGS := new")
        r = fx.plan()
        _affected(r, "check-b")
        assert r[0] == "full" and any("またぐ" in l for l in r[3]), r[3]


def case_mk_builtin_make(cs):
    # 独立レビュー 2 回目: 組み込み名 MAKE の再定義。$(MAKE) を使う検査は全部変わる
    sdk = ("MAKE := echo old\n" + FX_SDK.replace("python3 a.py $(MUT)", "@$(MAKE) a"))
    mk = FX_MAKEFILE.replace("python3 b.py $(abspath .)", "@$(MAKE) b")
    with fixture(cs, files={"Makefile": mk, "build/sdk.mk": sdk}) as fx:
        fx.edit("build/sdk.mk", "echo old", "echo new")
        _affected(fx.plan(), "check-a", "check-b")


def case_mk_target_specific(cs):
    # target-specific 変数: 値を変えればその検査、eval を介して別の検査に及べば全部
    sdk = FX_SDK.replace("check-a:\n\tpython3 a.py $(MUT)\n",
                         "check-a: V = old\ncheck-a:\n\t@echo $(V)\n")
    with fixture(cs, files={"build/sdk.mk": sdk}) as fx:
        fx.edit("build/sdk.mk", "V = old", "V = new")
        _only(fx.plan(), "check-a")
    sdk = FX_SDK.replace("check-a:\n\tpython3 a.py $(MUT)\n",
                         "check-a: V = $(call eval,FLAGS := old)\ncheck-a:\n\t@echo $(V)\n")
    mk = FX_MAKEFILE.replace("python3 b.py $(abspath .)", "@echo $(FLAGS)")
    with fixture(cs, files={"Makefile": mk, "build/sdk.mk": sdk}) as fx:
        fx.edit("build/sdk.mk", "FLAGS := old", "FLAGS := new")
        _affected(fx.plan(), "check-b")


def case_mk_computed_name(cs):
    sdk = "N := FLAGS\n$(N) = old\n" + FX_SDK.replace("python3 a.py $(MUT)", "@echo $(FLAGS)")
    with fixture(cs, files={"build/sdk.mk": sdk}) as fx:
        fx.edit("build/sdk.mk", "$(N) = old", "$(N) = new")
        _only(fx.plan(), "check-a")


def case_mk_default_override(cs):
    # ?= / 条件付き定義: 既定値の変更はその検査。コマンド行 (MAKEFLAGS) で上書きされて
    # いれば実効値は変わらない → 変異なし。選択器を呼ぶ make と同じ環境で両方の木を展開する
    for defn in ("FLAGS ?= old\n", "ifeq ($(origin FLAGS),undefined)\nFLAGS = old\nendif\n"):
        sdk = defn + FX_SDK.replace("python3 a.py $(MUT)", "@echo $(FLAGS)")
        with fixture(cs, files={"build/sdk.mk": sdk}) as fx:
            fx.edit("build/sdk.mk", "FLAGS ?= old" if "?=" in defn else "FLAGS = old",
                    "FLAGS ?= new" if "?=" in defn else "FLAGS = new")
            _only(fx.plan(), "check-a")
            saved = os.environ.get("MAKEFLAGS")
            os.environ["MAKEFLAGS"] = " -- FLAGS=cli"
            try:
                _fast(fx.plan(), ["check-a", "check-b"])
            finally:
                if saved is None:
                    del os.environ["MAKEFLAGS"]
                else:
                    os.environ["MAKEFLAGS"] = saved


def case_mk_secondexpansion(cs):
    # 別ファイルの .SECONDEXPANSION + 前提の $$(call eval,…)。列は check-b が先なので
    # 順の展開では見えず、逆順の展開で check-b が変わる → 全部
    mk = (".SECONDEXPANSION:\ndefine SET\nFLAGS := $(1)\nendef\n" +
          FX_MAKEFILE.replace("python3 b.py $(abspath .)", "@echo $(FLAGS)"))
    sdk = FX_SDK.replace("check-a check-b", "check-b check-a").replace(
        "check-a:\n", "check-a: $$(call eval,$$(call SET,old))\n")
    with fixture(cs, files={"Makefile": mk, "build/sdk.mk": sdk}) as fx:
        fx.edit("build/sdk.mk", "SET,old", "SET,new")
        r = fx.plan()
        _affected(r, "check-b")
        assert r[0] == "full", r[3]


def case_mk_order_cancel(cs):
    # 順でも逆順でも同じ値に見えるが単独では違う (a と c が同じ値、d が別の値、b が読む)。
    # 単独の展開と突き合わせて初めて順序依存と分かる → 全部
    sdk = ("CHECK_PAR_TARGETS := check-a check-b check-c check-d\n"
           ".PHONY: check-a check-b check-c check-d\n"
           "check-a:\n\t@echo a$(eval X := 1)\n"
           "check-b:\n\t@echo $(X)\n"
           "check-c:\n\t@echo c$(eval X := 1)\n"
           "check-d:\n\t@echo d$(eval X := 2)\n")
    mk = FX_MAKEFILE.replace(".PHONY: check-b\ncheck-b:\n\tpython3 b.py $(abspath .)\n", "")
    with fixture(cs, files={"Makefile": mk, "build/sdk.mk": sdk}) as fx:
        fx.edit("build/sdk.mk", "X := 2", "X := 3")
        r = fx.plan()
        _affected(r, "check-b")
        assert r[0] == "full", r[3]


def case_mk_failures(cs):
    # make が失敗 (missing separator)、基点が無い、列の読みが合わない、--files → 全部
    with fixture(cs) as fx:
        fx.edit("build/sdk.mk", "\tpython3 a.py", "  python3 a.py")
        _full(fx.plan(), "make")
    with fixture(cs) as fx:
        fx.edit("build/sdk.mk", "a.py", "a2.py")
        _full(fx.plan(base="no-such-ref", files=["build/sdk.mk"]), "基点")
        _full(fx.plan(files=["build/sdk.mk"], base=None), "基点版なし")
    with fixture(cs) as fx:
        fx.edit("build/sdk.mk", "CHECK_PAR_TARGETS := check-a check-b",
                "CHECK_PAR_TARGETS := $(filter-out check-b,check-a check-b)")
        _full(fx.plan(), "一致しない")


def case_mk_mixed(cs):
    # 他の変更ファイルの選択と合算。sdk/kapi.json は生成物を介するので全部のまま
    with fixture(cs) as fx:
        fx.edit("build/sdk.mk", "a.py", "a2.py")
        fx.write("src/b.py", "changed\n")
        _only(fx.plan(), "check-a", "check-b")
        fx.write("sdk/kapi.json", '{"v": 2}')
        _full(fx.plan(), "全体に影響")


def case_mk_git_versions(cs):
    # 分岐した基点は merge-base で比べる (main の先の変更は見ない)。コミット済み +
    # staged + 未 staged を全部見る。select() も基点を渡す
    with fixture(cs) as fx:
        d = fx.d
        git(d, "checkout", "-q", "-b", "work")
        fx.edit("build/sdk.mk", "a.py", "committed.py")
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
        fx.edit("Makefile", "python3 b.py", "python3 b.py --staged")
        git(d, "add", ".")
        _only(fx.plan(), "check-a", "check-b")
        fx.edit("Makefile", "echo old > $@", "echo work > $@")
        _full(fx.plan(), "ビルド")


def case_mk_real_tree(cs):
    # 実物の Makefile を基点 = HEAD で比べる: dry-run できて、列が字面の読みと一致し、差が無い
    _, vars_ = cs.read_makefiles()
    par = cs.check_lists(vars_)
    got, why = cs.make_narrow("HEAD", par)
    assert got == set(), (got, why)
    assert "変わった 0 本" in why, why


CASES = [case_inc_extract, case_hsync_protect, case_sh_pipe, case_bare_extract,
         case_readme, case_claude, case_docs_always, case_broad_only,
         case_submodule, case_nothing, case_single_stage, case_inc_dir_extract,
         case_notest_fast, case_notest_glob_wins, case_notest_guard,
         case_featgui_commit, case_lint_real,
         case_mk_recipe_line, case_mk_add_remove, case_mk_comment_only,
         case_mk_build_recipe, case_mk_prereq_file, case_mk_mut_flag,
         case_mk_define_state, case_mk_call_eval, case_mk_builtin_make,
         case_mk_target_specific, case_mk_computed_name, case_mk_default_override,
         case_mk_secondexpansion, case_mk_order_cancel, case_mk_failures,
         case_mk_mixed, case_mk_git_versions, case_mk_real_tree]


def run_cases(cs, quiet=False, cases=None):
    failed = []
    for c in CASES if cases is None else cases:
        try:
            c(cs)
            ok = True
        except Exception as e:           # noqa: BLE001 — 変異で何が起きても RED
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
MUTATIONS = [
    ('            self.pending.append(rel)\n', '            pass\n',
     "実装の .c / .inc の #include を辿らない (P2-1: .inc が表から落ちる)"),
    ('if "/" in s or s.endswith(BARE_EXTS):', 'if "/" in s:',
     "裸のファイル名 (README.md / CLAUDE.md) を候補にしない (P2-2)"),
    ('if not (t in broad and "**" in g)]', ']',
     "走査型の ** glob を「表に載っている」に数える (P2-1 の保険が外れる)"),
    ('run = hit | (set(m["docs_always"]) & set(par))', 'run = hit',
     "docs だけの変更で文書を読む検査を常には回さない (P2-2)"),
    ('        if mb != head:\n', '        if True:\n',
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
    # make の dry-run 比較 (2026-10-01)
    ('texts = {"base": _base_makefiles(ancestor), "work": _work_makefiles()}',
     'texts = {"base": _work_makefiles(), "work": _work_makefiles()}',
     "片側 (作業中の木) だけ展開して比べる"),
    ('    if res["base", "build"] != res["work", "build"]:', '    if False:',
     "ビルド (all) の展開を比べない"),
    ('            for name, combined in (("順", fwd), ("逆順", rev)):\n                if combined.get(u) != lines:',
     '            for name, combined in (("逆順", rev),):\n                if combined.get(u) != lines:',
     "順の展開と突き合わせない (逆順だけ)"),
    ('            for name, combined in (("順", fwd), ("逆順", rev)):\n                if combined.get(u) != lines:',
     '            for name, combined in (("順", fwd),):\n                if combined.get(u) != lines:',
     "逆順の展開と突き合わせない (順だけ)"),
    ('                if combined.get(u) != lines:', '                if combined.get(u) != lines and u != t:',
     "検査自身の目標は単独とまとめの不一致に数えない (順・逆順で同じ値に見える順序依存を見逃す)"),
    ('        if why:\n            return None, "検査をまたぐ影響', '        if False:\n            return None, "検査をまたぐ影響',
     "検査をまたぐ影響を全部に倒さない"),
    ('        except MakeError as e:\n            return None,', '        except MakeError as e:\n            return set(),',
     "make の失敗を全部に倒さない"),
    ('    return changed | added, why', '    return changed, why',
     "列に足した検査を変異込みにしない"),
    ('                if lists["work"] != list(par):', '                if False:',
     "make の検査の列と字面の列の不一致を見ない"),
    ('.replace(cwd, "<ROOT>")', '',
     "一時ディレクトリの番地を正規化しない ($(abspath) の違いで全部が変わって見える)"),
    ('        p = subprocess.run(cmd, capture_output=True, stdin=subprocess.DEVNULL,\n',
     '        p = subprocess.run(cmd, capture_output=True, stdin=subprocess.DEVNULL, env={"PATH": os.environ["PATH"]},\n',
     "選択器を呼ぶ make の環境 (MAKEFLAGS のコマンド行変数) を引き継がない"),
    ('["-n", "-B", "--trace", "MUTATE=1"]', '["-n", "--trace", "MUTATE=1"]',
     "-B を付けない (木にある前提ファイルの recipe の変更を見ない)"),
    ('["-n", "-B", "--trace", "MUTATE=1"]', '["-n", "-B", "--trace"]',
     "MUTATE=1 で展開しない (変異ありの recipe を比べない)"),
    ('        m = TRACE_RE.match(line)\n        if m:', '        m = None\n        if m:',
     "--trace の行で目標ごとに切らない"),
    ('    changed = {t for t in old & new if res["base", t] != res["work", t]}',
     '    changed = set()',
     "展開が変わった検査を選ばない"),
    ('    ancestor = p.stdout.strip()\n    if p.returncode != 0 or not ancestor:',
     '    ancestor = base\n    if p.returncode != 0 or not ancestor:',
     "分岐した基点と直接比較する (merge-base でない)"),
    ('            if matches(f, mk):\n                mk_hits.append(f)',
     '            if matches(f, mk) or f.endswith(".json"):\n                mk_hits.append(f)',
     "sdk/kapi.json も make の比較で絞る (生成物を介した変化は make -n に現れない)"),
    ('base=None if files is not None else base)', 'base=None)',
     "select から基点を渡さず絞り込めない"),
]


def mutate():
    original = SRC.read_text(encoding="utf-8")
    bad = 0
    glob_cases = [c for c in CASES if not c.__name__.startswith("case_mk_")]
    mk_cases = [c for c in CASES if c.__name__.startswith("case_mk_")]
    for i, (old, new, why) in enumerate(MUTATIONS, 1):
        if original.count(old) != 1:
            print("MUTATION %d NOT APPLICABLE: %s" % (i, why), flush=True)
            bad += 1
            continue
        try:
            cs = load(original.replace(old, new))
            # make の変異は fixture (速い) を先に、実物の木の lint は後ろに
            cases = mk_cases + glob_cases if i > 13 else glob_cases + mk_cases
            failed = run_cases(cs, quiet=True, cases=cases)
        except Exception as e:           # noqa: BLE001
            print("MUTATION %d INVALID (load): %r" % (i, e), flush=True)
            bad += 1
            continue
        if failed:
            print("MUTATION %d RED (%s): %s" % (i, failed[0], why), flush=True)
        else:
            print("MUTATION %d **GREEN (見逃し)**: %s" % (i, why), flush=True)
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
