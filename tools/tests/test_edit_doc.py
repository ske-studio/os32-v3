"""edit_gui の本文と libos32gui の textcore をホストで踏む (票 TASK_EDIT_GUI 受入 E8 / E10)。

実 FS にも実エミュレータにも触れない。**実物のソース**
(`userland/rust/edit_gui/src/doc.rs` と `userland/rust/libos32gui/src/textcore.rs`) を
そのまま取り込み、保存の経路だけ KAPI を差し替える。

  python3 -B tools/tests/test_edit_doc.py [--mutate]

なぜホストだけで足りるか: 本文の操作 (挿入・削除・改行・折り返しの桁) と
「見える範囲」の切り出しが壊れていたら、GUI で触っても意味がない。逆にここが
通れば、ゲストで見るのは**描画と入力の配線**だけになる (票 §5 の E8)。

--mutate は**否定側**。票が名指しした 4 つの壊し方で RED になることを見る:

  1. UTF-8 の途中で折り返す
  2. 見える範囲の計算がずれる (行が飛ぶ / 重なる)
  3. `WK_TEXTBOX` の振る舞いが変わる (E10。決裁 A1 で共通化した下請けを壊す)
  4. 保存できなかったのに成功と答える

**静的な突き合わせ**も 1 件だけ持つ: 実物の `widget.rs` の textbox が
`textcore` を通っていること。ここが切れていると変異 3 が textbox に届かず、
E10 を見たことにならない。
"""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/tests"))
import os32api_host  # noqa: E402
import mutpar  # noqa: E402

TEXTCORE = ROOT / "userland/rust/libos32gui/src/textcore.rs"
TEXTCORE_TESTS = ROOT / "userland/rust/libos32gui/host/textcore_tests.rs"
DOC = ROOT / "userland/rust/edit_gui/src/doc.rs"
DOC_TESTS = ROOT / "userland/rust/edit_gui/host/doc_tests.rs"
WIDGET = ROOT / "userland/rust/libos32gui/src/widget.rs"
GUI_APP = ROOT / "userland/rust/libos32gui/src/app.rs"

# ---------------------------------------------------------------- 変異
# (名前, 当てるファイル, 元, 後) — すべて**コンパイルは通り、実行時に落ちる**もの。
MUTATIONS = [
    # 1. UTF-8 の途中で折り返す (§4-27 の再発)。
    (
        "wrap_splits_utf8",
        TEXTCORE,
        "        let step = seq_len(s[i]);\n        let q = i + step;",
        "        let step = 1;\n        let q = i + step;",
    ),
    # 1b. 全角を 1 桁として数える (折り返しが 1 桁はみ出る)。
    (
        "wide_char_counted_as_one",
        TEXTCORE,
        "        COLS_KANJI\n    }\n}",
        "        COLS_ANK\n    }\n}",
    ),
    # 2. 見える範囲の計算がずれる — 行が 1 本飛ぶ。
    (
        "view_skips_a_row",
        DOC,
        "                if cur >= nth {\n                    if n >= out.len() {",
        "                if cur > nth {\n                    if n >= out.len() {",
    ),
    # 2b. 見える範囲が重なる — 論理行ごとに先頭へ戻さない。
    (
        "view_rows_overlap",
        DOC,
        "            nth = 0;\n            i += 1;",
        "            i += 1;",
    ),
    # 3. WK_TEXTBOX の振る舞いが変わる (E10)。入る大きさを見ない版。
    (
        "textbox_overflows",
        TEXTCORE,
        "    if len + k > buf.len() {\n        return None;\n    }",
        "    if len + k > buf.len() {\n        return Some(len);\n    }",
    ),
    # 3b. 全角を 1 バイトずつ扱う版 (BS が壊れた文字を残す)。
    (
        "textbox_boundary_is_one_byte",
        TEXTCORE,
        "    while p > 0 {\n        p -= 1;\n        if (s[p] & 0xC0) != 0x80 {\n            break;\n        }\n    }\n    p\n}",
        "    if p > 0 {\n        p -= 1;\n    }\n    p\n}",
    ),
    # 4. 保存できなかったのに成功と答える (短い write を見ない)。
    (
        "short_write_reported_as_ok",
        DOC,
        "            if (n as usize) != line.len() {\n"
        "                unsafe { (os32api::api().sys_close)(fd) };\n"
        "                return ERR_IO; /* 短い write は失敗 */\n"
        "            }",
        "",
    ),
    # 4a'. 改行の短い write を見ない版。
    (
        "short_newline_write_reported_as_ok",
        DOC,
        "        if n != 1 {\n"
        "            unsafe { (os32api::api().sys_close)(fd) };\n"
        "            return ERR_IO;\n"
        "        }",
        "",
    ),
    # 5. 末尾の改行を空行として数える (穴 H16)。開いて保存するだけで 1 バイト伸びる。
    (
        "trailing_newline_grows_the_file",
        DOC,
        "        self.nlines = if len == 0 && li > 0 { li } else { li + 1 };",
        "        self.nlines = li + 1;",
    ),
    # 4b. open の失敗を成功にする。
    (
        "open_failure_swallowed",
        DOC,
        "    if fd < 0 {\n        return fd;\n    }\n    let nl = [b'\\n'];",
        "    if fd < 0 {\n        return 0;\n    }\n    let nl = [b'\\n'];",
    ),
]


def build_and_run(out, label, src_root=ROOT):
    """試験 2 本を組み立てて走らせる。戻り = (returncode, 出力)。

    src_root は取り込む実物の木 (変異のときは一時ディレクトリの写しの木)。"""
    def at(path):
        return src_root / path.relative_to(ROOT)
    textcore, textcore_tests = at(TEXTCORE), at(TEXTCORE_TESTS)
    doc, doc_tests = at(DOC), at(DOC_TESTS)
    rlib = os32api_host.build(out)

    # libos32gui (ホスト版) は textcore だけ。doc.rs の `use libos32gui::textcore`
    # をそのまま解決させる — 写しを作らない。
    gui_src = out / "gui_host.rs"
    gui_src.write_text(
        "#![allow(dead_code)]\n"
        f'#[path="{textcore}"] pub mod textcore;\n'
    )
    gui_rlib = out / "liblibos32gui.rlib"
    subprocess.run(
        ["rustc", "--edition=2021", "--crate-type=rlib",
         "--crate-name=libos32gui", str(gui_src), "-o", str(gui_rlib)],
        cwd=src_root, check=True,
    )

    results = []
    # (1) textcore + WK_TEXTBOX の振る舞い
    root = out / "textcore_root.rs"
    root.write_text(
        "#![allow(dead_code)]\n"
        f'#[path="{textcore}"] pub mod textcore;\n'
        f'#[path="{textcore_tests}"] mod textcore_tests;\n'
    )
    exe = out / ("textcore-tests-" + label)
    subprocess.run(
        ["rustc", "--edition=2021", "--test", str(root), "-o", str(exe)],
        cwd=src_root, check=True,
    )
    results.append(("textcore", exe))

    # (2) edit_gui の本文
    root = out / "doc_root.rs"
    root.write_text(
        "#![allow(dead_code)]\n"
        f'#[path="{doc}"] pub mod doc;\n'
        f'#[path="{doc_tests}"] mod doc_tests;\n'
    )
    exe = out / ("doc-tests-" + label)
    subprocess.run(
        ["rustc", "--edition=2021", "--test", str(root),
         "-L", str(out),
         "--extern", "os32api=" + str(rlib),
         "--extern", "libos32gui=" + str(gui_rlib),
         "-o", str(exe)],
        cwd=src_root, check=True,
    )
    results.append(("doc", exe))

    rc = 0
    text = ""
    for name, exe in results:
        p = subprocess.run([str(exe), "--test-threads=1"], cwd=src_root,
                           capture_output=True, timeout=180)
        text += p.stdout.decode("utf-8", "replace")
        if p.returncode != 0:
            rc = p.returncode
    return rc, text


def static_checks():
    """実物の textbox が共通の下請けを通っていること (E10 の前提)。"""
    src = WIDGET.read_text(encoding="utf-8")
    bad = []
    # tb_insert / tb_remove / prev_boundary / next_boundary の中身だけを見る。
    for fn, want in (
        ("fn tb_insert", "textcore::insert"),
        ("fn tb_remove", "textcore::remove"),
        ("fn prev_boundary", "textcore::prev_boundary"),
        ("fn next_boundary", "textcore::next_boundary"),
    ):
        m = re.search(re.escape(fn) + r"\(.*?\n\}", src, re.S)
        if not m:
            bad.append(f"{fn} が widget.rs に無い")
        elif want not in m.group(0):
            bad.append(
                f"{fn} が {want} を通っていない — "
                "共通の下請けから外れると変異が textbox に届かない (E10)"
            )
    if "WK_TEXTBOX: u8 = 4" not in (
        ROOT / "userland/rust/libos32gui/src/uistate.rs"
    ).read_text(encoding="utf-8"):
        bad.append("WK_TEXTBOX の番号が動いた (決裁 A1: 末尾追記のみ)")
    bad += focus_notify_checks(src)
    return bad


def focus_notify_checks(widget_src):
    """アプリ発の `set_focus` が `on_widget_focus` として届くこと (穴 H13)。

    入力から合成したフォーカス移動は `WidgetOut` でループへ返るが、アプリが
    自分で呼んだぶんは返す先が無い。これを捨てると、**フォーカスを
    `on_widget_focus` だけで追っているアプリは自分で移した後に編集面へ
    戻ったと分からなくなる**。文字は `Text` 経由で入るので
    **カーソルキーだけが死ぬ**という、目視では原因の分からない形で出た。

    実行時には GUI サーバが要るのでホストでは踏めない。**配線の規則**として
    見る (textbox が textcore を通っている検査と同じ形)。
    """
    bad = []
    m = re.search(r"pub fn set_focus\(.*?\n\}", widget_src, re.S)
    if not m:
        bad.append("pub fn set_focus が widget.rs に無い")
    elif "pending_focus" not in m.group(0):
        bad.append(
            "set_focus が通知を溜めていない — アプリ発のフォーカス移動が "
            "on_widget_focus として届かない (穴 H13: カーソルキーだけが死ぬ)"
        )
    if "pub fn take_pending_focus" not in widget_src:
        bad.append("take_pending_focus が無い (ループが通知を取り出せない)")

    app_src = GUI_APP.read_text(encoding="utf-8")
    if "fn drain_pending_focus" not in app_src:
        bad.append("drain_pending_focus が app.rs に無い (穴 H13)")
    else:
        m = re.search(r"pub fn run_vt.*?\n\}", app_src, re.S)
        if not m:
            bad.append("run_vt がループの本体に無い")
        elif m.group(0).count("drain_pending_focus") < 2:
            bad.append(
                "ループが通知を配っていない — 窓を組む間のぶんと、"
                "ハンドラの中で移したぶんの 2 か所で配る (穴 H13)"
            )
    return bad


def one_mutation(item):
    """変異 1 本: 一時ディレクトリの写しの木を壊して組んで回す (実物は読むだけ)。
    (印字, 見逃し) を返す。"""
    i, (name, target, old, new) = item
    original = target.read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-30s SKIP (当て先が見つからない)" % name, 1
    rel = str(target.relative_to(ROOT))
    with tempfile.TemporaryDirectory(prefix="os32-edit-doc-mut-") as tmp:
        tmp = pathlib.Path(tmp)
        # #[path] で取り込む 4 本は実体にする (相対の mod 解決も写しの中で閉じる)
        real = {str(p.relative_to(ROOT))
                for p in (TEXTCORE, TEXTCORE_TESTS, DOC, DOC_TESTS)}
        tree = mutpar.mutant_tree(ROOT, tmp / "tree",
                                  {rel: original.replace(old, new, 1)},
                                  real=real)
        out = tmp / "out"
        out.mkdir()
        try:
            rc, _text = build_and_run(out, "m%d" % i, src_root=tree)
        except subprocess.CalledProcessError:
            return "MUTATE %-30s RED (コンパイルが通らない)" % name, 0
    if rc == 0:
        return ("MUTATE %-30s **GREEN のまま = 試験が規則を見ていない**" % name,
                1)
    return "MUTATE %-30s RED (期待どおりに落ちた)" % name, 0


def run_mutations():
    """否定側。変異は一時ディレクトリの写しにだけ当てる (mutpar で並列、
    check-par で回せる)。"""
    return mutpar.run_with_control(one_mutation, list(enumerate(MUTATIONS)),
                                   (len(MUTATIONS), ("control", DOC, "", "")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mutate", action="store_true")
    args = ap.parse_args()

    bad = static_checks()
    for b in bad:
        print("STATIC FAIL " + b, flush=True)
    if bad:
        return 1
    print("STATIC PASS (textbox は textcore を通っている / WK_TEXTBOX は 4 のまま"
          " / set_focus が on_widget_focus を配る)", flush=True)

    with tempfile.TemporaryDirectory(prefix="os32-edit-doc-") as tmp:
        rc, text = build_and_run(pathlib.Path(tmp), "base")
        print(text, end="", flush=True)
        if rc != 0:
            print("EXIT %d (本体が RED)" % rc, flush=True)
            return rc
    print("BASE GREEN", flush=True)

    if args.mutate:
        n = run_mutations()
        if n:
            print("MUTATE 総括: %d 件が期待どおりに落ちなかった" % n, flush=True)
            return 1
        print("MUTATE 総括: 全 %d 件が RED" % len(MUTATIONS), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
