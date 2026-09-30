"""tools/check_docs_status.py (状態行の語彙の検査) のホスト試験。

記録: tools/tests/docs_status_tdd.md
語彙の正典: docs/POLICY_DEV.md §8

一時ディレクトリに小さな木 (docs/POLICY_DEV.md の語彙の表と docs/tasks/ の票) を
作り、実物の tools/check_docs_status.py (または変異させた写し) を import して
check(root) の結果を見る。実物のリポジトリの木が通ることも 1 ケースとして見る。

  python3 -B tools/tests/test_docs_status.py            # 全ケース
  python3 -B tools/tests/test_docs_status.py --mutate   # 否定側
"""
import contextlib
import importlib.util
import io
import os
import pathlib
import re
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "tools/check_docs_status.py"

POLICY = """# 開発ポリシー

## §8. 文書の状態行

<!-- status-vocab:begin -->
| 語 | 意味 |
|---|---|
| 計画 | a |
| 受入完了・実機確認待ち | b |
| 受入完了 | c |
| 完了記録 | d |
<!-- status-vocab:end -->
"""


def load(path=None):
    spec = importlib.util.spec_from_file_location(
        "check_docs_status_under_test", str(path or SRC))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


FAILED = []


def check(cond, what):
    if not cond:
        FAILED.append(what)


_n = [0]


def tree(tmp, files, policy=POLICY):
    """files: {docs/tasks からの相対: 本文}。木の根を返す。"""
    _n[0] += 1
    root = os.path.join(tmp, "t%d" % _n[0])
    os.makedirs(os.path.join(root, "docs", "tasks"))
    if policy is not None:
        with open(os.path.join(root, "docs", "POLICY_DEV.md"), "w", encoding="utf-8") as f:
            f.write(policy)
    for rel, body in files.items():
        p = os.path.join(root, "docs", "tasks", rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", encoding="utf-8") as f:
            f.write(body)
    return root


def head(status_line, pad=0):
    return "# 票\n\n" + "本文\n" * pad + status_line + "\n\n本文\n"


def run_check(mod, root):
    try:
        return mod.check(root), None
    except Exception as e:           # 語彙の読み失敗など
        return None, e


def case_real_tree(mod, tmp):
    probs, err = run_check(mod, str(ROOT))
    check(err is None and probs == [], "実物の木が通らない: %r %r" % (err, probs))


def case_vocab_ok(mod, tmp):
    root = tree(tmp, {
        "a/TASK_A.md": head("> 発行: PM (2026-09-29) / 状態: **計画 (2026-09-29)**"),
        "a/TASK_B.md": head("> 状態: **受入完了 (2026-09-29)** — 根拠"),
        "a/TASK_C.md": head("状態: 完了記録。"),
        "a/NOTE.md": "# 設計メモ\n\n状態は無い\n",
    })
    probs, err = run_check(mod, root)
    check(err is None and probs == [], "語彙どおりの票が落ちた: %r %r" % (err, probs))


def case_longest_word(mod, tmp):
    root = tree(tmp, {"TASK_X.md": head("> 状態: **受入完了・実機確認待ち (2026-09-29)** — K3")})
    probs, err = run_check(mod, root)
    check(err is None and probs == [], "受入完了・実機確認待ち が通らない: %r" % (probs,))


def case_outside_vocab(mod, tmp):
    root = tree(tmp, {"x/TASK_X.md": head("> 状態: **実装済み・レビュー待ち**")})
    probs, err = run_check(mod, root)
    check(err is None and probs and "語彙の外" in probs[0] and "TASK_X.md:3" in probs[0],
          "語彙の外の状態行を見逃した: %r" % (probs,))


def case_not_boundary(mod, tmp):
    root = tree(tmp, {"TASK_X.md": head("> 状態: **受入完了済み**"),
                      "TASK_Y.md": head("> 状態: **計画中**")})
    probs, err = run_check(mod, root)
    check(err is None and probs is not None and len(probs) == 2,
          "語の後ろに文字が続くものを通した: %r" % (probs,))


def case_required_missing(mod, tmp):
    root = tree(tmp, {
        "a/TASK_A.md": "# 票\n\n本文だけ\n",
        "a/HANDOVER_2026-09-29.md": "# 引き継ぎ\n",
        "a/CHECKLIST_2026-09-26.md": "# 手順\n",
        "a/PLAN.md": "# 計画\n",
        "a/S0_PLAN_2026-09-13.md": "# 計画\n",
        "a/README.md": "# 読んで\n",
    })
    probs, err = run_check(mod, root)
    check(err is None and probs is not None and len(probs) == 5
          and all("状態行がありません" in p for p in probs)
          and not any("README" in p for p in probs),
          "必須の状態行の欠落の判定が違う: %r" % (probs,))


def case_old_line_ignored(mod, tmp):
    root = tree(tmp, {
        "TASK_A.md": head("> 状態: **受入完了**\n>\n> 発行: PM / それまでの状態: **方針確定**"),
        "TASK_B.md": head("> 発行: PM / それまでの状態: **方針確定**"),
    })
    probs, err = run_check(mod, root)
    check(err is None and probs is not None and len(probs) == 1
          and "TASK_B.md" in probs[0] and "状態行がありません" in probs[0],
          "「それまでの状態:」の扱いが違う: %r" % (probs,))


def case_head_limit(mod, tmp):
    root = tree(tmp, {"TASK_A.md": head("> 状態: **計画**", pad=12),
                      "TASK_B.md": head("> 状態: **計画**", pad=7)})
    probs, err = run_check(mod, root)
    check(err is None and probs is not None and len(probs) == 1
          and "TASK_A.md" in probs[0],
          "先頭 12 行の範囲が違う: %r" % (probs,))


def case_first_line_only(mod, tmp):
    root = tree(tmp, {"TASK_A.md": head("> 状態: **方針確定**\n> 状態: **計画**")})
    probs, err = run_check(mod, root)
    check(err is None and probs is not None and len(probs) == 1,
          "最初の状態行だけを見ていない: %r" % (probs,))


def case_policy_markers(mod, tmp):
    root = tree(tmp, {"TASK_A.md": head("> 状態: **計画**")},
                policy=POLICY.replace("<!-- status-vocab:end -->", ""))
    probs, err = run_check(mod, root)
    check(err is not None, "語彙の印が無いのに通した: %r" % (probs,))
    root = tree(tmp, {"TASK_A.md": head("> 状態: **計画**")},
                policy="<!-- status-vocab:begin -->\n| 語 | 意味 |\n|---|---|\n<!-- status-vocab:end -->\n")
    probs, err = run_check(mod, root)
    check(err is not None, "語彙の表が空なのに通した: %r" % (probs,))


def case_vocab_from_policy(mod, tmp):
    # 表に無い語 (実物の語彙にはある「撤回」) は、この木では語彙の外
    root = tree(tmp, {"TASK_A.md": head("> 状態: **撤回**")})
    probs, err = run_check(mod, root)
    check(err is None and probs is not None and len(probs) == 1,
          "語彙を表から読んでいない: %r" % (probs,))


def case_archive_ignored(mod, tmp):
    root = tree(tmp, {"TASK_A.md": head("> 状態: **計画**")})
    p = os.path.join(root, "docs", "archive", "x", "TASK_OLD.md")
    os.makedirs(os.path.dirname(p))
    with open(p, "w", encoding="utf-8") as f:
        f.write(head("> 状態: **追補あり**"))
    probs, err = run_check(mod, root)
    check(err is None and probs == [], "docs/archive を見ている: %r" % (probs,))


def case_main_rc(mod, tmp):
    root = tree(tmp, {"TASK_A.md": head("> 状態: **追補あり**")})
    old = sys.argv
    try:
        sys.argv = ["x", "--root", root]
        with contextlib.redirect_stdout(io.StringIO()):
            rc_bad = mod.main()
        root2 = tree(tmp, {"TASK_A.md": head("> 状態: **計画**")})
        sys.argv = ["x", "--root", root2]
        with contextlib.redirect_stdout(io.StringIO()):
            rc_ok = mod.main()
    finally:
        sys.argv = old
    check(rc_bad == 1 and rc_ok == 0, "main の終了コード: bad=%r ok=%r" % (rc_bad, rc_ok))


CASES = {
    "real_tree": case_real_tree,
    "vocab_ok": case_vocab_ok,
    "longest_word": case_longest_word,
    "outside_vocab": case_outside_vocab,
    "not_boundary": case_not_boundary,
    "required_missing": case_required_missing,
    "old_line_ignored": case_old_line_ignored,
    "head_limit": case_head_limit,
    "first_line_only": case_first_line_only,
    "policy_markers": case_policy_markers,
    "vocab_from_policy": case_vocab_from_policy,
    "archive_ignored": case_archive_ignored,
    "main_rc": case_main_rc,
}

# (正規表現, 置換, 何を壊すか) — 実物の写しに 1 か所ずつ当てる
MUTATIONS = [
    (r'r"\(\?:\^\|> \|/ \)状態:', 'r"状態:', "状態行の前置の制限を外す (それまでの状態: も数える)"),
    (r"if no > HEAD_LINES:\n                break", "pass", "先頭 12 行の制限を外す"),
    (r"HEAD_LINES = 12", "HEAD_LINES = 20", "先頭の行数を広げる"),
    (r"if not rest or rest\[0\] in BOUNDARY:", "if True:", "語の後ろの区切りを見ない"),
    (r"if REQUIRED_RE.search\(os.path.basename\(p\)\):", "if False:", "必須の状態行を見ない"),
    (r"\|PLAN\.\*\\\.md\$", "", "PLAN を必須から外す"),
    (r"TASK_\|HANDOVER_\|CHECKLIST_", "TASK_|CHECKLIST_", "HANDOVER を必須から外す"),
    (r"raise ValueError\(\"%s の語彙の表が空です\" % POLICY\)", "pass", "空の語彙表を通す"),
    (r"if BEGIN not in text or END not in text:", "if False:", "語彙の印の欠落を見ない"),
    (r"if v.startswith\(\"\*\*\"\):", "if False:", "値の先頭の ** を外さない"),
    (r"return no, m.group\(1\)", "pass", "状態行を拾わない"),
    (r'problems.append\("%s:%d: 語彙の外', 'pass  # ("%s:%d: 語彙の外', "語彙の外を報告しない"),
    (r"return 1\n    print\(\"OK", "return 0\n    print(\"OK", "問題があっても rc=0"),
    (r"base = os.path.join\(root, TASKS\)", "base = os.path.join(root, 'docs')", "archive まで見る"),
]


def run(mod, names, tmp):
    global FAILED
    bad = 0
    for name in names:
        FAILED = []
        CASES[name](mod, tmp)
        rc = 1 if FAILED else 0
        for f in FAILED:
            print("  FAIL %s: %s" % (name, f), flush=True)
        print("EXIT %s=%d" % (name, rc), flush=True)
        bad += rc
    print("SUMMARY %d/%d PASS" % (len(names) - bad, len(names)), flush=True)
    return bad


def mutate(tmp):
    global FAILED
    original = SRC.read_text(encoding="utf-8")
    bad = 0
    for i, (pattern, repl, why) in enumerate(MUTATIONS, 1):
        text, n = re.subn(pattern, lambda m: repl, original, count=1)
        if n != 1:
            print("MUTATION %d NOT APPLICABLE: %s" % (i, why), flush=True)
            bad += 1
            continue
        # **実物は書き換えない** — 写しの上で変異させる (check-par で並列可)。
        path = pathlib.Path(tmp) / ("mut%d.py" % i)
        path.write_text(text, encoding="utf-8")
        try:
            mod = load(path)
        except Exception:
            print("MUTATION %d RED (import): %s" % (i, why), flush=True)
            continue
        hits = 0
        for name in CASES:
            FAILED = []
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    CASES[name](mod, tmp)
            except Exception:
                FAILED.append("raised")
            hits += 1 if FAILED else 0
        status = "RED" if hits else "**GREEN (見逃し)**"
        print("MUTATION %d %s (%d 件): %s" % (i, status, hits, why), flush=True)
        bad += not hits
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    names = [a for a in args if not a.startswith("--")] or list(CASES)
    with tempfile.TemporaryDirectory(prefix="os32-docs-status-") as tmp:
        mod = load()
        print("HOST import PASS (real tools/check_docs_status.py)", flush=True)
        rc = run(mod, names, tmp)
        if "--mutate" in args:
            rc += mutate(tmp)
    sys.exit(bool(rc))
