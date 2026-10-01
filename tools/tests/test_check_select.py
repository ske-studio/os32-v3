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
  * build/*.mk は検査列の名前と登録済み検査の前提・recipe だけの変更なら
    当該検査だけ変異込み。列の削除、混在、基点の分岐、未コミット版も確認する。
    変数・非検査規則・define・条件分岐・列以外の改行継続は安全側へ倒す。
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
    # 逐次の 2 段目は無い: full は列の全部を 1 段で変異込み
    _, vars_ = cs.read_makefiles()
    par = cs.check_lists(vars_)
    assert "check-sh-status-host" in par and "check-kapi-layout-host" in par
    mode, st, mu = run_plan(cs, ["Makefile"])
    assert mode == "full" and st == set(par) and mu == set(par), mode


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


# ---------------------------------------------------------------- make syntax / conservative fallback
MAKE_BEFORE = "CHECK_PAR_TARGETS := check-a check-b\ncheck-a: input\n\tpython3 a.py\ncheck-b:\n\tpython3 b.py\nFLAGS := old\nnormal:\n\techo old\n"


def make_fixture_plan(cs, before, after, extra=(), second=None):
    originals = cs.load_map, cs.read_makefiles, cs.make_change
    _, names = cs.make_units(after)
    cs.load_map = lambda: dict(ignore=[], full=["build/*.mk", "Makefile", "sdk/kapi.json"],
                               docs_only=["**/*.md"], notest=[], broad=[], docs_always=[],
                               checks={"check-a": ["src/a.c"], "check-b": ["src/b.c"]})
    cs.read_makefiles = lambda: ({}, {"CHECK_PAR_TARGETS": names})
    def compare(path, base, par):
        if base is None:
            return None, "base missing"
        pair = second if path == "build/other.mk" else (before, after)
        return cs.narrow_make_change(*pair, ["check-a", "check-b"], par)
    cs.make_change = compare
    try:
        return cs.plan(["build/sdk.mk", *extra], base="fixture")
    finally:
        cs.load_map, cs.read_makefiles, cs.make_change = originals


def case_make_add(cs):
    after = MAKE_BEFORE.replace("check-a check-b", "check-a check-b check-c") + \
        "check-c: new-input\n\tpython3 c.py\n"
    mode, st, mu, lines = make_fixture_plan(cs, MAKE_BEFORE, after)
    assert mode == "sel" and mu == ["check-c"], (mode, mu)
    assert st == ["check-a", "check-b", "check-c"]
    assert any("絞り込み" in l for l in lines), lines


def case_make_recipe(cs):
    for after in (MAKE_BEFORE.replace("a.py", "changed.py"),
                  MAKE_BEFORE.replace("check-a: input", "check-a: other"),
                  MAKE_BEFORE.replace("check-a: input\n\tpython3 a.py\n", "")):
        mode, _, mu, _ = make_fixture_plan(cs, MAKE_BEFORE, after)
        assert mode == "sel" and mu == ["check-a"], (mode, mu)


def case_make_remove(cs):
    after = MAKE_BEFORE.replace("check-a check-b", "check-a")
    mode, st, mu, _ = make_fixture_plan(cs, MAKE_BEFORE, after)
    assert mode == "fast" and st == ["check-a"] and not mu, (mode, st, mu)
    after = after.replace("check-b:\n\tpython3 b.py\n", "")
    assert make_fixture_plan(cs, MAKE_BEFORE, after)[2] == []


def case_make_variable(cs):
    after = MAKE_BEFORE.replace("FLAGS := old", "FLAGS := new")
    mode, st, mu, lines = make_fixture_plan(cs, MAKE_BEFORE, after)
    assert mode == "full" and mu == st, (mode, mu)
    assert any("全部" in l and "変数" in l for l in lines), lines


def case_make_noncheck(cs):
    after = MAKE_BEFORE.replace("echo old", "echo new")
    assert make_fixture_plan(cs, MAKE_BEFORE, after)[0] == "full"
    after = MAKE_BEFORE + "check-unlisted:\n\techo new\n"
    assert make_fixture_plan(cs, MAKE_BEFORE, after)[0] == "full"


def case_make_unsupported(cs):
    # Only the literal check-name column may span physical lines.
    before = MAKE_BEFORE.replace("check-a check-b", "check-a \\\n    check-b")
    after = before.replace("check-b\n", "check-b check-c\n", 1) + "check-c:\n\techo c\n"
    assert make_fixture_plan(cs, before, after)[2] == ["check-c"]
    pairs = [
        (MAKE_BEFORE, MAKE_BEFORE.replace("a.py", "a.py \\\n\t--new")),
        (MAKE_BEFORE + "FLAGS += old \\\n    tail\n",
         MAKE_BEFORE + "FLAGS += new \\\n    tail\n"),
        (MAKE_BEFORE + "define BODY\ncheck-a:\n\techo old\nendef\n",
         MAKE_BEFORE + "define BODY\ncheck-a:\n\techo new\nendef\n"),
        (MAKE_BEFORE + "ifeq (x,x)\ncheck-a:\n\techo old\nendif\n",
         MAKE_BEFORE + "ifeq (x,x)\ncheck-a:\n\techo new\nendif\n"),
        (MAKE_BEFORE, MAKE_BEFORE + "include new.mk\n"),
        (MAKE_BEFORE + "\tCHECK_PAR_TARGETS := check-a check-b\n",
         MAKE_BEFORE + "\tCHECK_PAR_TARGETS := check-a\n"),
        (MAKE_BEFORE, MAKE_BEFORE + "%.o: %.c\n\techo pattern\n"),
        (MAKE_BEFORE, MAKE_BEFORE.replace("check-a: input", "check-a: FLAGS=new")),
        (MAKE_BEFORE, MAKE_BEFORE.replace("CHECK_PAR_TARGETS :=", "CHECK_PAR_TARGETS =")),
        (MAKE_BEFORE, MAKE_BEFORE + "# swallowed \\\ninclude new.mk\n"),
    ]
    for prefix in ("define BODY", "  define BODY", "override export define BODY", "  ifdef FLAG"):
        end = "endif" if "ifdef" in prefix else "endef"
        body = MAKE_BEFORE.replace("check-a: input\n\tpython3 a.py\n", "")
        pairs.append((body + prefix + "\ncheck-a:\n\techo old\n" + end + "\n",
                      body + prefix + "\ncheck-a:\n\techo new\n" + end + "\n"))
    pairs.extend([
        (MAKE_BEFORE, MAKE_BEFORE.replace("check-a check-b", "check-b check-a")),
        (MAKE_BEFORE, MAKE_BEFORE.replace("check-a check-b", "check-a check-b check-b")),
        (MAKE_BEFORE, MAKE_BEFORE + "check-a:\n\techo duplicate\n"),
        (MAKE_BEFORE, MAKE_BEFORE + ".RECIPEPREFIX := >\n"),
        (MAKE_BEFORE, MAKE_BEFORE + "$(eval generated)\n"),
    ])
    for before, after in pairs:
        result = make_fixture_plan(cs, before, after)
        assert result[0] == "full", (before, after, result)


def case_make_mixed(cs):
    after = MAKE_BEFORE.replace("a.py", "changed.py")
    assert set(make_fixture_plan(cs, MAKE_BEFORE, after, ["src/b.c"])[2]) == {"check-a", "check-b"}
    pair = ("check-b:\n\techo old\n", "check-b:\n\techo new\n")
    assert set(make_fixture_plan(cs, MAKE_BEFORE, after, ["build/other.mk"], pair)[2]) == {"check-a", "check-b"}
    for extra in (["Makefile"], ["sdk/kapi.json"], ["unknown"]):
        assert make_fixture_plan(cs, MAKE_BEFORE, after, extra)[0] == "full"
    pair = ("FLAGS := old\n", "FLAGS := new\n")
    assert make_fixture_plan(cs, MAKE_BEFORE, after, ["build/other.mk"], pair)[0] == "full"
    assert cs.plan(["build/sdk.mk"])[0] == "full", "--files has no versions"


def case_make_git_versions(cs):
    # A branch whose base diverged: compare merge-base, and read the working
    # copy (both staged and unstaged), not just HEAD or the diff hunk.
    with tempfile.TemporaryDirectory(prefix="os32-ckmake-") as d:
        git(d, "init", "-q", "-b", "main")
        git(d, "config", "user.email", "t@example.invalid")
        git(d, "config", "user.name", "t")
        pathlib.Path(d, "build").mkdir()
        path = pathlib.Path(d, "build/sdk.mk")
        path.write_text(MAKE_BEFORE)
        git(d, "add", ".")
        git(d, "commit", "-q", "-m", "fixture")
        git(d, "checkout", "-q", "-b", "work")
        path.write_text(MAKE_BEFORE.replace("a.py", "committed.py"))
        git(d, "add", ".")
        git(d, "commit", "-q", "-m", "recipe")
        git(d, "checkout", "-q", "main")
        path.write_text(MAKE_BEFORE.replace("FLAGS := old", "FLAGS := base-new"))
        git(d, "add", ".")
        git(d, "commit", "-q", "-m", "diverged base")
        git(d, "checkout", "-q", "work")
        old_root = cs.ROOT
        cs.ROOT = d
        try:
            got, _ = cs.make_change("build/sdk.mk", "main", ["check-a", "check-b"])
            assert got == {"check-a"}, got
            old_map, old_make = cs.load_map, cs.read_makefiles
            cs.load_map = lambda: dict(ignore=[], full=["build/*.mk"], docs_only=[],
                                       notest=[], broad=[], docs_always=[], checks={})
            cs.read_makefiles = lambda: ({}, {"CHECK_PAR_TARGETS": ["check-a", "check-b"]})
            out = io.StringIO()
            try:
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
                    assert cs.select("main") == 0
                assert "CC_MODE=sel\n" in out.getvalue(), out.getvalue()
                assert "CC_MUT1=check-a\n" in out.getvalue(), out.getvalue()
            finally:
                cs.load_map, cs.read_makefiles = old_map, old_make
            path.write_text(path.read_text().replace("b.py", "staged.py"))
            git(d, "add", ".")
            assert cs.make_change("build/sdk.mk", "main", ["check-a", "check-b"])[0] == {"check-a", "check-b"}
            path.write_text(path.read_text().replace("FLAGS := old", "FLAGS := work-new"))
            assert cs.make_change("build/sdk.mk", "main", ["check-a", "check-b"])[0] is None
            path.unlink()
            assert cs.make_change("build/sdk.mk", "main", ["check-a", "check-b"])[0] is None
            assert cs.make_change("build/new.mk", "main", ["check-a", "check-b"])[0] is None
        finally:
            cs.ROOT = old_root


CASES = [case_inc_extract, case_hsync_protect, case_sh_pipe, case_bare_extract,
         case_readme, case_claude, case_docs_always, case_broad_only,
         case_submodule, case_nothing, case_single_stage, case_inc_dir_extract,
         case_notest_fast, case_notest_glob_wins, case_notest_guard,
         case_featgui_commit, case_lint_real, case_make_add, case_make_recipe,
         case_make_remove, case_make_variable, case_make_noncheck,
         case_make_unsupported, case_make_mixed, case_make_git_versions]


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
    ('            if kind != "rule" or key not in listed:\n'
     '                return None, "変数・非検査規則・未対応構文の変更"',
     '            if kind != "rule" or key not in listed:\n                continue', "変数・非検査規則を絞り込む (安全側を外す)"),
    ('            if "\\\\\\n" in value or "\\\\\\r\\n" in value:',
     '            if False:', "recipe の改行継続を絞り込む"),
    ('        if depth:', '        if False:',
     "define・条件分岐内を通常の検査規則として絞る"),
    ('elif i == start + 1 and (not line.strip() or line.startswith("#")):',
     'elif not line.strip() or line.startswith("#"):',
     "改行継続コメントが飲み込む行を無視する"),
    ('    return affected & set(par),', '    return set(),',
     "変更した検査の変異を選ばない"),
    ('ancestor = git("merge-base", base, "HEAD")[0]', 'ancestor = base',
     "分岐した基点と直接比較する"),
    ('base=None if files is not None else base)', 'base=None)',
     "select から基点を渡さず絞り込めない"),
    ('            after = f.read()', '            after = show(path)',
     "未コミット版を無視する"),
]


def mutate():
    original = SRC.read_text(encoding="utf-8")
    bad = 0
    for i, (old, new, why) in enumerate(MUTATIONS, 1):
        if original.count(old) != 1:
            print("MUTATION %d NOT APPLICABLE: %s" % (i, why), flush=True)
            bad += 1
            continue
        try:
            cs = load(original.replace(old, new))
            # New selection mutants should hit the make fixtures before the
            # expensive real-tree lint; keep all cases as a backstop.
            cases = sorted(CASES, key=lambda c: not c.__name__.startswith("case_make_")) if i > 13 else CASES
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
