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


CASES = [case_inc_extract, case_hsync_protect, case_sh_pipe, case_bare_extract,
         case_readme, case_claude, case_docs_always, case_broad_only,
         case_submodule, case_nothing, case_single_stage, case_inc_dir_extract,
         case_notest_fast, case_notest_glob_wins, case_notest_guard,
         case_featgui_commit, case_lint_real]


def run_cases(cs, quiet=False):
    failed = []
    for c in CASES:
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
            failed = run_cases(cs, quiet=True)
        except Exception as e:           # noqa: BLE001
            failed = ["load: %r" % e]
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
