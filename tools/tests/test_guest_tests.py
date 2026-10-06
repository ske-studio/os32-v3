#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ゲスト一括実行ランナーのホスト試験 (票 docs/archive/test/TASK_TEST_RUNNER.md §5 R7)。

記録: tools/tests/guest_tests_tdd.md

**R7 が本体。** 生成と集計はホストだけで試験でき、そこが壊れていたらゲストで
回しても意味がない。だからこの試験は `tools/guest_tests.py` の**純粋な部分**
(一覧 → スクリプト生成 / 出力 → 集計 / 食い違いの検出 / 見張りの判定) を贋物の
入力だけで全部踏み、**触る部分**は贋のゲストと贋の時計を注入して同じ道を通す。

**エミュレータにも NP21/W にも時計にも一切触らない。** 待ち時間は 1 秒も無い
(贋の時計を進めるだけ)。

使い方:

    python3 -B tools/tests/test_guest_tests.py [--mutate]

`--mutate` は**否定側**。tools/guest_tests.py の写し (一時ディレクトリの木) を 1 か所ずつ壊し、
この試験が**実行時に**落ちることを見る。壊し方はどれも Python として正しく、
import は通る — 構文エラーで落ちるだけなら試験の目が働いたことにならない。

    1  食い違い (0 なのに FAIL) を見逃す
    2  見張りが発火しない
    3  固まった試験を名指ししない
    4  /host が無いのに合格にする
    5  落ちた試験 (139) で後続を打ち切る
    6  前回の出力が混ざるのを見逃す (R8)
    7  1 なのに PASS を見逃す
    8  予約値 (139) を合格として読む
    9  全体の上限で結果を全部捨てる
"""

import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
TARGET = TOOLS / "guest_tests.py"
LIST_FILE = ROOT / "tools/tests/guest_tests.txt"

sys.path.insert(0, str(TOOLS))
import guest_tests as gt                                        # noqa: E402
sys.path.insert(0, str(ROOT / "tools/tests"))
import mutpar                                                   # noqa: E402


# ---------------------------------------------------------------------------
#  小道具
# ---------------------------------------------------------------------------

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)
        print("  FAIL %s" % msg, flush=True)
    return cond


def eq(got, want, msg):
    return check(got == want, "%s (得たもの: %r / 期待: %r)" % (msg, got, want))


def entries(*specs):
    ents, errs = gt.parse_list("\n".join(specs) + "\n")
    assert not errs, errs
    return ents


def blk(name, rc, body=()):
    out = ["%s %s" % (gt.MARK_RUN, name)]
    out.extend(body)
    out.append("%s %s %s" % (gt.MARK_RC, name, rc))
    return out


def tape(token, *chunks):
    lines = ["%s %s %s" % (gt.MARK_SUITE, gt.MARK_BEGIN, token)]
    for c in chunks:
        lines.extend(c)
    return lines


def done(token):
    return ["%s %s %s" % (gt.MARK_SUITE, gt.MARK_DONE, token)]


class Clock(object):
    """贋の時計。sleep で進むだけ。**本物の time には触らない。**"""

    def __init__(self, on_tick=None):
        self.t = 0.0
        self.on_tick = on_tick

    def now(self):
        return self.t

    def sleep(self, dt):
        self.t += dt
        if self.on_tick:
            self.on_tick(self.t)


class FakeGuest(object):
    """贋のゲスト。HTTP も NP21/W も無い。

    `source` を受けると、台本 (`steps` = [(時刻, 行の並び), ...]) に従って
    出力ファイルへ追記する。時刻は Clock が進めたぶんだけ。
    """

    def __init__(self, test_dir, steps, probe_ok=True, reopen_ok=True):
        self.test_dir = pathlib.Path(test_dir)
        self.steps = list(steps)
        self.probe_ok = probe_ok
        self.reopen_ok = reopen_ok
        self.lines = []
        self.reopened = 0
        self.started = False

    # --- ゲストの口 ---
    def cmd(self, line, timeout=None):
        self.lines.append(line)
        if line.startswith("echo ") and gt.GUEST_PROBE in line and self.probe_ok:
            token = line.split()[1]
            (self.test_dir / gt.HOST_PROBE_NAME).write_text(token + "\n",
                                                            encoding="utf-8")
            return ""
        if line.startswith("source "):
            self.started = True
            self.pump(0.0)
        return ""

    def key(self, seq=None, text=None):
        pass

    def reopen_shell(self):
        self.reopened += 1
        return self.reopen_ok

    # --- 台本を流す ---
    def pump(self, now):
        if not self.started:
            return
        due = [s for s in self.steps if s[0] <= now]
        self.steps = [s for s in self.steps if s[0] > now]
        if not due:
            return
        path = self.test_dir / gt.HOST_OUT_NAME
        with open(path, "a", encoding="utf-8") as f:
            for _, lines in due:
                for ln in lines:
                    f.write(ln + "\n")


def drive(steps, probe_ok=True, reopen_ok=True, stall=gt.STALL_SECS_MIN,
          total=gt.STALL_SECS_MIN * 8, token="tk1", host_dir=None, tmp=None):
    """run_suite を贋のゲストと贋の時計で回す。返り値: (rep, rc, guest, log)"""
    if tmp is None:
        with tempfile.TemporaryDirectory(prefix="os32-guest-tests-") as directory:
            return drive(steps, probe_ok, reopen_ok, stall, total, token,
                         host_dir, directory)
    host_dir = host_dir if host_dir is not None else tmp
    test_dir = pathlib.Path(host_dir) / gt.HOST_SUBDIR
    if os.path.isdir(host_dir):
        test_dir.mkdir(parents=True, exist_ok=True)
    guest = FakeGuest(test_dir, steps, probe_ok=probe_ok, reopen_ok=reopen_ok)
    clock = Clock(on_tick=guest.pump)
    log = []
    rep, rc = gt.run_suite(guest, host_dir, ENTS, stall_secs=stall,
                           total_secs=total, poll_every=gt.POLL_EVERY_DEFAULT,
                           token=token, now=clock.now, sleep=clock.sleep,
                           log=log.append)
    return rep, rc, guest, "\n".join(log)


ENTS = entries("aa", "bb", "cc")


# ---------------------------------------------------------------------------
#  1. 一覧 → Entry
# ---------------------------------------------------------------------------

def t_parse_list():
    print("== 1. 一覧の読み取り ==", flush=True)
    ents, errs = gt.parse_list(
        "# 頭のコメント\n"
        "\n"
        "klibc_test   # 末尾のコメントは落ちる\n"
        "restest all\n"
        "   \n"
        "stat_t\n")
    eq(errs, [], "正しい一覧で誤りは出ない")
    eq([e.name for e in ents], ["klibc_test", "restest", "stat_t"],
       "名前は行の最初の語")
    eq([e.cmd for e in ents],
       ["klibc_test", "restest all", "stat_t"], "命令は行全体 (引数を保つ)")

    _, errs = gt.parse_list("aa\naa\n")
    check(len(errs) == 1 and "重なって" in errs[0],
          "名前が重なったら誤り (目印が区別できなくなる)")
    _, errs = gt.parse_list("../etc/passwd\n")
    check(len(errs) == 1, "名前に使えない字は誤り")
    _, errs = gt.parse_list("# コメントだけ\n")
    check(len(errs) == 1 and "空" in errs[0], "空の一覧は誤り")

    real, errs = gt.parse_list(LIST_FILE.read_text(encoding="utf-8"))
    eq(errs, [], "実物の tools/tests/guest_tests.txt が読める")
    check(len(real) >= 16, "実物の一覧が第 1 陣の 16 本以上ある (%d)" % len(real))
    names = [e.name for e in real]
    for banned in ("kbd_echo", "mouse_test", "blit_test", "tile_bench",
                   "gfx200_test", "asset_demo", "rotate_test", "hal_test",
                   "ring3_fault", "ring3_guard", "crash", "kstr_bench"):
        check(banned not in names,
              "画面デモ・対話・ベンチ・落ちるのが正解のものを入れない: %s" % banned)
    for want in ("klibc_test", "math_test", "restest", "stat_t", "e2test",
                 "host_test", "db_test"):
        check(want in names, "第 1 陣の %s が一覧にある" % want)
    check(real[names.index("restest")].cmd == "restest all",
          "restest は引数つき (argc < 2 で使い方を出して終わる)")


# ---------------------------------------------------------------------------
#  2. 一覧 → スクリプト
# ---------------------------------------------------------------------------

def t_build_script():
    print("== 2. スクリプトの生成 ==", flush=True)
    s = gt.build_script(entries("aa", "bb cc"), "tok")
    lines = s.splitlines()
    eq(lines[0], "echo RUNTESTS BEGIN tok > /host/test/out.txt",
       "先頭は `>` で切り詰める (前回の出力を混ぜない — R8)")
    eq(lines[1:4],
       ["echo RUN aa >> /host/test/out.txt",
        "aa >> /host/test/out.txt",
        "echo RC aa $? >> /host/test/out.txt"],
       "試験ごとに 3 行 (票 §2-1 の形そのまま)")
    eq(lines[4:7],
       ["echo RUN bb >> /host/test/out.txt",
        "bb cc >> /host/test/out.txt",
        "echo RC bb $? >> /host/test/out.txt"],
       "引数つきの行もそのまま並ぶ")
    eq(lines[-1], "echo RUNTESTS DONE tok >> /host/test/out.txt",
       "最後は完了印")
    eq(len(lines), 1 + 3 * 2 + 1, "行数は 1 + 3n + 1")
    check(s.endswith("\n"), "最終行にも改行がある (source が最後の行を落とさない)")
    check(";" not in s, "`;` での連結を使わない (シェルに無い — 票 §1)")
    check("for " not in s and "while " not in s,
          "ループを使わない (シェルに無い — 票 §1)")
    check("|" not in s, "パイプを使わない (出力はすべてファイルへ)")

    # 落ちた試験の後ろも無条件に並ぶ = 1 本の事故で後続が消えない (R3)
    s3 = gt.build_script(entries("aa", "bb", "cc"), "t")
    for name in ("aa", "bb", "cc"):
        check("echo RUN %s >> " % name in s3,
              "%s の RUN 行が無条件に並ぶ" % name)
    check("if " not in s3, "条件で後続を飛ばさない")

    # 並びは一覧のとおり = どの試験がどの順で走るかがホストに残る (再現できる)
    idx = [s3.index("echo RUN %s " % n) for n in ("aa", "bb", "cc")]
    check(idx == sorted(idx), "並びは一覧のとおり")


# ---------------------------------------------------------------------------
#  3. 出力 → 構造
# ---------------------------------------------------------------------------

def t_parse_output():
    print("== 3. 出力の読み取り ==", flush=True)
    text = "\n".join(tape("tk",
                          blk("aa", 0, ["aa: PASS 3/3"]),
                          blk("bb", 1, ["noise", "bb: FAIL 1/2"])) + done("tk"))
    out = gt.parse_output(text)
    eq(out.begin_token, "tk", "BEGIN の合い言葉を拾う")
    eq(out.done_token, "tk", "DONE の合い言葉を拾う")
    eq(out.order, ["aa", "bb"], "出た順を保つ")
    eq(out.blocks["aa"].rc, 0, "RC を数として拾う")
    eq(out.blocks["aa"].summary.kind, "PASS", "集計行の種別")
    eq((out.blocks["bb"].summary.passed, out.blocks["bb"].summary.total), (1, 2),
       "集計行の数")

    # CRLF (HostDrv 越しに書かれても読めること)
    out = gt.parse_output(text.replace("\n", "\r\n"))
    eq(out.blocks["aa"].rc, 0, "CRLF でも読める")

    # 他人の集計行を拾わない (名前は固定文字列の約束)
    out = gt.parse_output("\n".join(tape("t", blk("aa", 0, ["bb: PASS 9/9",
                                                            "aa: PASS 1/1"]))))
    eq(out.blocks["aa"].summary.raw.strip(), "aa: PASS 1/1",
       "他の試験の名前で始まる行は自分の集計行にしない")

    # 最終行の集計行を採る (途中に紛らわしい行があっても)
    out = gt.parse_output("\n".join(tape("t", blk("aa", 1, ["aa: PASS 1/1",
                                                            "aa: FAIL 1/2"]))))
    eq(out.blocks["aa"].summary.kind, "FAIL", "最後に出た集計行を採る")

    # SKIP は理由つき
    out = gt.parse_output("\n".join(tape("t", blk("aa", 2, ["aa: SKIP no host"]))))
    eq(out.blocks["aa"].summary.reason, "no host", "SKIP の理由を拾う")

    # $? が展開されなかった場合 (行が字面のまま)
    out = gt.parse_output("\n".join(tape("t", ["RUN aa", "RC aa $?"])))
    eq(out.blocks["aa"].rc, None, "RC が数でなければ None (勝手に 0 にしない)")

    # RC 行が無い = 途中で切れた
    out = gt.parse_output("\n".join(tape("t", ["RUN aa", "aa: PASS 1/1"])))
    eq(out.blocks["aa"].closed, False, "RC が無ければ閉じていない")


# ---------------------------------------------------------------------------
#  4. 2 つの答えの突き合わせ (票 §2-4) — この試験の中心
# ---------------------------------------------------------------------------

def t_classify():
    print("== 4. 終了コードと集計行の突き合わせ ==", flush=True)
    P = lambda n, m: gt.Summary("PASS", n, m, None, "aa: PASS %d/%d" % (n, m))
    F = lambda n, m: gt.Summary("FAIL", n, m, None, "aa: FAIL %d/%d" % (n, m))
    S = lambda r: gt.Summary("SKIP", None, None, r, "aa: SKIP %s" % r)

    # --- 票 §2-4 の表、そのまま ---
    eq(gt.classify(0, P(3, 3))[0], "pass", "0 + PASS n/n = 合格")
    eq(gt.classify(1, F(1, 2))[0], "fail", "1 + FAIL m/n = 不合格")
    eq(gt.classify(2, S("no db"))[0], "skip", "2 + SKIP = 実行しなかった")

    # 予約値は**集計行が出ないのが正常**。値で種別が分かる。
    eq(gt.classify(139, None)[0], "crash", "139 = 例外で畳んだ")
    eq(gt.classify(126, None)[0], "nostart", "126 = 起こせなかった")
    eq(gt.classify(127, None)[0], "notfound", "127 = 実行ファイルが無い")
    eq(gt.classify(130, None)[0], "intr", "130 = 中断")
    for rc in (139, 126, 127, 130):
        k, _ = gt.classify(rc, None)
        check(not gt.KINDS[k][1], "%d は合格に数えない" % rc)
        check(k != "mismatch",
              "%d で集計行が無いのを「集計行が無い = 異常」と一緒くたにしない" % rc)

    # --- 食い違い = 不合格。**片方を選ばない** ---
    k, why = gt.classify(0, F(1, 2))
    eq(k, "mismatch", "0 なのに FAIL は食い違い")
    check("0" in why and "FAIL" in why, "食い違いの中身を言う (%r)" % why)
    eq(gt.classify(1, P(3, 3))[0], "mismatch", "1 なのに PASS は食い違い")
    eq(gt.classify(1, P(1, 2))[0], "mismatch",
       "1 なのに PASS (数が半端でも) は食い違い — 種別だけを見る門")
    eq(gt.classify(1, S("no db"))[0], "mismatch",
       "1 なのに SKIP は食い違い (不合格と「実行しなかった」を混ぜない)")
    eq(gt.classify(0, None)[0], "mismatch", "0 なのに集計行が無いのは食い違い")
    eq(gt.classify(1, None)[0], "mismatch", "1 なのに集計行が無いのは食い違い")
    eq(gt.classify(2, None)[0], "mismatch", "2 なのに集計行が無いのは食い違い")
    eq(gt.classify(2, P(1, 1))[0], "mismatch", "2 なのに PASS は食い違い")
    eq(gt.classify(0, S("x"))[0], "mismatch", "0 なのに SKIP は食い違い")
    eq(gt.classify(0, P(2, 3))[0], "mismatch",
       "0 + PASS でも数が釣り合わなければ食い違い")
    eq(gt.classify(1, F(3, 3))[0], "mismatch",
       "1 + FAIL でも全件合格の数なら食い違い")
    eq(gt.classify(139, P(3, 3))[0], "mismatch",
       "落ちた値なのに集計行があるのは食い違い")

    # --- その他 ---
    eq(gt.classify(None, None)[0], "incomplete", "RC が無ければ incomplete")
    eq(gt.classify(7, None)[0], "local", "3〜125 は票が定義してよい個別の値")
    check(not gt.KINDS["local"][1], "個別の値は合格に数えない")
    eq(gt.classify(200, None)[0], "mismatch", "約束事に無い値は食い違い")

    # 合格に数えるのは pass と skip だけ
    for k in gt.KINDS:
        check(gt.KINDS[k][1] == (k in ("pass", "skip")),
              "合格に数えるのは pass / skip だけ (%s)" % k)


# ---------------------------------------------------------------------------
#  5. 集計
# ---------------------------------------------------------------------------

def t_aggregate():
    print("== 5. 集計 ==", flush=True)
    ok_text = "\n".join(tape("tk",
                             blk("aa", 0, ["aa: PASS 1/1"]),
                             blk("bb", 0, ["bb: PASS 2/2"]),
                             blk("cc", 0, ["cc: PASS 3/3"])) + done("tk"))
    rep = gt.aggregate(ENTS, ok_text, "tk")
    check(rep.ok, "全件合格なら ok")
    eq(gt.report_exit_code(rep), gt.RC_OK, "全件合格の終了コードは 0")

    # R2: 1 本だけ壊しても残りは走り切る
    text = "\n".join(tape("tk",
                          blk("aa", 0, ["aa: PASS 1/1"]),
                          blk("bb", 1, ["bb: FAIL 1/2"]),
                          blk("cc", 0, ["cc: PASS 3/3"])) + done("tk"))
    rep = gt.aggregate(ENTS, text, "tk")
    eq([r.kind for r in rep.rows], ["pass", "fail", "pass"],
       "壊れた 1 本だけ不合格、残りはそのまま")
    eq(len(rep.failed), 1, "不合格は 1 件")
    eq(gt.report_exit_code(rep), gt.RC_TESTS_FAILED, "1 件でも不合格なら非ゼロ")

    # R3: 落ちた試験 (139) の後続が走り、集計にも残る
    text = "\n".join(tape("tk",
                          blk("aa", 0, ["aa: PASS 1/1"]),
                          blk("bb", 139),
                          blk("cc", 0, ["cc: PASS 3/3"])) + done("tk"))
    rep = gt.aggregate(ENTS, text, "tk")
    eq(len(rep.rows), 3, "落ちた試験があっても一覧の全件が表に出る")
    eq([r.kind for r in rep.rows], ["pass", "crash", "pass"],
       "139 は crash、後続は合格のまま (**打ち切らない**)")
    eq(gt.report_exit_code(rep), gt.RC_TESTS_FAILED, "落ちたら非ゼロ")
    body = gt.format_report(rep, ENTS)
    check(body.index("\ncc ") > body.index("\nbb "),
          "報告の表でも落ちた bb の**後ろに** cc の行が出る")

    # R5: 食い違いは不合格として報告される
    text = "\n".join(tape("tk",
                          blk("aa", 0, ["aa: FAIL 1/2"]),
                          blk("bb", 1, ["bb: PASS 2/2"]),
                          blk("cc", 0, ["cc: PASS 3/3"])) + done("tk"))
    rep = gt.aggregate(ENTS, text, "tk")
    eq([r.kind for r in rep.rows], ["mismatch", "mismatch", "pass"],
       "食い違いは両向きとも不合格")
    check(gt.report_exit_code(rep) != gt.RC_OK, "食い違いがあれば非ゼロ")
    check("MISMATCH" in gt.format_report(rep, ENTS), "報告に MISMATCH と出る")

    # SKIP は不合格と区別する (実行しなかった)
    text = "\n".join(tape("tk",
                          blk("aa", 2, ["aa: SKIP no buffers"]),
                          blk("bb", 0, ["bb: PASS 2/2"]),
                          blk("cc", 0, ["cc: PASS 3/3"])) + done("tk"))
    rep = gt.aggregate(ENTS, text, "tk")
    eq(rep.rows[0].kind, "skip", "SKIP は skip")
    eq(gt.report_exit_code(rep), gt.RC_OK, "SKIP だけなら不合格にしない")
    check("no buffers" in gt.format_report(rep, ENTS), "SKIP の理由を表に出す")

    # 一覧にあるのに走っていない
    text = "\n".join(tape("tk", blk("aa", 0, ["aa: PASS 1/1"])) + done("tk"))
    rep = gt.aggregate(ENTS, text, "tk")
    eq([r.kind for r in rep.rows], ["pass", "missing", "missing"],
       "走っていないものは missing (黙って消さない)")
    check(gt.report_exit_code(rep) != gt.RC_OK, "missing があれば非ゼロ")

    # 完了印が無い
    text = "\n".join(tape("tk", blk("aa", 0, ["aa: PASS 1/1"]),
                          blk("bb", 0, ["bb: PASS 2/2"]),
                          blk("cc", 0, ["cc: PASS 3/3"])))
    rep = gt.aggregate(ENTS, text, "tk")
    check(not rep.done, "完了印が無ければ done にしない")
    check(gt.report_exit_code(rep) != gt.RC_OK,
          "全件合格でも完了印が無ければ非ゼロ (最後まで走った証拠が無い)")

    # R8: 前回の出力が混ざる
    stale = "\n".join(tape("OLD", blk("aa", 0, ["aa: PASS 1/1"]),
                           blk("bb", 0, ["bb: PASS 2/2"]),
                           blk("cc", 0, ["cc: PASS 3/3"])) + done("OLD"))
    rep = gt.aggregate(ENTS, stale, "tk")
    check(rep.problems, "合い言葉が違えば問題として挙げる")
    check(any("前回" in p for p in rep.problems),
          "前回の出力が残っていると言う: %r" % rep.problems)
    check(gt.report_exit_code(rep) != gt.RC_OK,
          "**中身が全部 PASS でも**合い言葉が違えば非ゼロ")

    # BEGIN が無い (スクリプトが走り出していない)
    rep = gt.aggregate(ENTS, "", "tk")
    check(rep.problems, "空の出力は問題")
    check(gt.report_exit_code(rep) != gt.RC_OK, "空の出力は非ゼロ")

    # 同じ名前が 2 回
    dup = "\n".join(tape("tk", blk("aa", 0, ["aa: PASS 1/1"]),
                         blk("aa", 0, ["aa: PASS 1/1"]),
                         blk("bb", 0, ["bb: PASS 2/2"]),
                         blk("cc", 0, ["cc: PASS 3/3"])) + done("tk"))
    rep = gt.aggregate(ENTS, dup, "tk")
    check(any("2 回" in p for p in rep.problems), "RUN が 2 回出たら問題")

    # 一覧に無いものが出力にある
    extra = "\n".join(tape("tk", blk("aa", 0, ["aa: PASS 1/1"]),
                           blk("bb", 0, ["bb: PASS 2/2"]),
                           blk("cc", 0, ["cc: PASS 3/3"]),
                           blk("zz", 0, ["zz: PASS 1/1"])) + done("tk"))
    rep = gt.aggregate(ENTS, extra, "tk")
    check(any("一覧に無い" in p for p in rep.problems),
          "一覧に無い結果が混ざったら問題")


# ---------------------------------------------------------------------------
#  6. 固まったときに**どれで**止まったかを言う (票 §3)
# ---------------------------------------------------------------------------

def t_running_test():
    print("== 6. 止まった場所の名指し ==", flush=True)
    text = "\n".join(tape("tk", blk("aa", 0, ["aa: PASS 1/1"]),
                          ["RUN bb", "bb: 途中まで"]))
    eq(gt.running_test(text), "bb", "始まって終わっていない試験を名指しする")
    text = "\n".join(tape("tk", blk("aa", 0), blk("bb", 0)))
    eq(gt.running_test(text), None, "全部閉じていれば None")
    text = "\n".join(tape("tk", blk("aa", 0)) + done("tk"))
    eq(gt.running_test(text), None, "完了印の後は None")
    eq(gt.running_test(""), None, "空でも落ちない")
    text = "\n".join(tape("tk", ["RUN aa"]))
    eq(gt.running_test(text), "aa", "1 本目で固まっても名指しできる")


def t_watch():
    print("== 7. 見張り ==", flush=True)
    w = gt.Watch(60, 600).start(0)
    eq(w.observe(10, 100), "run", "伸びていれば走り続ける")
    eq(w.observe(50, 200), "run", "伸び続ければ走り続ける")
    eq(w.observe(100, 200), "run", "50 秒しか止まっていなければまだ待つ")
    eq(w.observe(110, 200), "stall", "60 秒伸びなければ stall")

    w = gt.Watch(60, 120).start(0)
    for t in range(0, 130, 10):
        st = w.observe(t, t)            # 伸び続けるが全体の上限に当たる
    eq(st, "timeout", "伸び続けても全体の上限で timeout")

    # [V3] 短く切らせない
    try:
        gt.Watch(59, 600)
        check(False, "60 秒未満の見張りを断る [V3]")
    except ValueError as exc:
        check("V3" in str(exc) or "60" in str(exc),
              "断る理由に [V3] / 60 秒が出る")
    try:
        gt.Watch(120, 100)
        check(False, "全体の上限が 1 本より短い指定を断る")
    except ValueError:
        pass
    check(gt.STALL_SECS_MIN >= 60, "1 本あたりの下限は 60 秒以上 [V3]")
    check(gt.STALL_SECS_DEFAULT >= gt.STALL_SECS_MIN, "既定は下限以上")


def t_hostdrv():
    print("== 8. /host の確認 ==", flush=True)
    # **2 つの門を別々に踏む。** 片方だけ壊しても、もう片方に隠れて
    # 気づけない形にしない。
    ok, why = gt.hostdrv_verdict(False, None, "tk")
    check(not ok, "置き場も印も無ければ断る")
    check("HostDrv" in why, "理由に HostDrv が出る")
    ok, why = gt.hostdrv_verdict(False, "tk\n", "tk")
    check(not ok, "印があってもホスト側に置き場が無ければ断る (門 1 だけを見る)")
    check("HostDrv" in why, "門 1 の理由に HostDrv が出る")
    ok, why = gt.hostdrv_verdict(True, None, "tk")
    check(not ok, "ゲストの印が現れなければ断る (実機には HostDrv が無い)")
    check("実機" in why or "/host" in why, "理由に /host の不在が出る")
    ok, _ = gt.hostdrv_verdict(True, "OLD\n", "tk")
    check(not ok, "別の内容なら断る")
    ok, why = gt.hostdrv_verdict(True, "tk\n", "tk")
    check(ok and not why, "印が一致すれば通す")


# ---------------------------------------------------------------------------
#  9. 触る部分 — 贋のゲストと贋の時計で同じ道を通す
# ---------------------------------------------------------------------------

def t_run_all_pass():
    print("== 9. 通し (全件合格) ==", flush=True)
    steps = [(0.0, tape("tk1", blk("aa", 0, ["aa: PASS 1/1"]),
                        blk("bb", 0, ["bb: PASS 2/2"]),
                        blk("cc", 0, ["cc: PASS 3/3"])) + done("tk1"))]
    rep, rc, guest, log = drive(steps)
    eq(rc, gt.RC_OK, "全件合格なら 0")
    check(rep.ok, "報告も ok")
    eq(guest.reopened, 1, "rshell を開き直してから走らせる (票 §2-2 の 2)")
    check(any(l.startswith("source ") for l in guest.lines),
          "source でスクリプトを流す")
    check("aa" in log and "bb" in log and "cc" in log, "表に全件が出る")


def t_run_crash_continues():
    print("== 10. 通し (139 で後続が走る) ==", flush=True)
    steps = [(0.0, tape("tk1", blk("aa", 0, ["aa: PASS 1/1"]),
                        blk("bb", 139),
                        blk("cc", 0, ["cc: PASS 3/3"])) + done("tk1"))]
    rep, rc, _, log = drive(steps)
    check(rc != gt.RC_OK, "落ちた試験があれば非ゼロ")
    eq([r.kind for r in rep.rows], ["pass", "crash", "pass"],
       "139 の後続も走り切って表に出る")
    check("CRASH" in log, "報告に CRASH と出る")


def t_run_stall():
    print("== 11. 通し (固まる) ==", flush=True)
    # cc を始めたきり黙る台本。贋の時計だけが進む。
    steps = [(0.0, tape("tk1", blk("aa", 0, ["aa: PASS 1/1"]),
                        blk("bb", 0, ["bb: PASS 2/2"]),
                        ["RUN cc"]))]
    rep, rc, _, log = drive(steps, stall=60, total=600)
    eq(rc, gt.RC_STALLED, "固まったら stall の終了コード")
    eq(rep.stalled_at, "cc", "**どれで止まったか**を名指しする")
    check("cc" in log, "報告にも名前が出る")
    eq(len(rep.rows), 3, "そこまでの結果も出す (全部捨てない)")
    eq([r.kind for r in rep.rows[:2]], ["pass", "pass"],
       "止まる前の結果はそのまま読める")
    eq(rep.rows[2].kind, "incomplete", "止まった試験は incomplete")


def t_run_timeout():
    print("== 12. 通し (全体の上限) ==", flush=True)
    # **伸び続けている**のに全体の上限に当たる (固まったのとは別の事情)。
    steps = [(0.0, tape("tk1", blk("aa", 0, ["aa: PASS 1/1"]))),
             (30.0, ["cc はまだ走っている 1"]),
             (60.0, blk("bb", 0, ["bb: PASS 2/2"])),
             (90.0, ["cc はまだ走っている 2"]),
             (120.0, ["cc はまだ走っている 3"]),
             (300.0, blk("cc", 0, ["cc: PASS 3/3"]) + done("tk1"))]
    rep, rc, _, log = drive(steps, stall=60, total=140)
    eq(rc, gt.RC_STALLED, "上限に当たったら非ゼロ")
    check(rep is not None, "**そこまでの結果を出してから**落ちる (全部捨てない)")
    check(rep.truncated, "上限に当たったと記録する")
    check(not rep.stalled, "伸びていたので「固まった」とは言わない")
    eq(rep.rows[0].kind, "pass", "上限前の結果は読める")
    check("aa" in log, "報告に出ている")


def t_run_no_host():
    print("== 13. 通し (/host が無い) ==", flush=True)
    with tempfile.TemporaryDirectory(prefix="os32-guest-tests-") as tmp:
        missing = os.path.join(tmp, "no-such-dir")
        rep, rc, guest, log = drive([], host_dir=missing)
        eq(rc, gt.RC_NO_PREREQ, "/host が無ければ前提不足で非ゼロ")
        check(rep is None, "報告を作らない (合格にしない)")
        eq(guest.reopened, 0, "前提が無い時点で止まる (走らせない)")
        check("/host" in log, "断る理由を言う [V4]")

        # 置き場はあるが、ゲストに /host がマウントされていない (実機)
        rep, rc, _, log = drive([], probe_ok=False)
        eq(rc, gt.RC_NO_PREREQ, "ゲストの印が現れなければ非ゼロ")
        check(rep is None, "黙って合格にしない")


def t_run_unreachable():
    print("== 14. 通し (ゲストに手が届かない) ==", flush=True)

    class Dead(FakeGuest):
        def cmd(self, line, timeout=None):
            raise gt.GuestUnreachable("繋がらない")

    with tempfile.TemporaryDirectory(prefix="os32-guest-tests-") as tmp:
        (pathlib.Path(tmp) / gt.HOST_SUBDIR).mkdir(parents=True)
        log = []
        clock = Clock()
        rep, rc = gt.run_suite(Dead(pathlib.Path(tmp) / gt.HOST_SUBDIR, []), tmp,
                               ENTS, stall_secs=60, total_secs=600, token="tk1",
                               now=clock.now, sleep=clock.sleep, log=log.append)
        eq(rc, gt.RC_NO_PREREQ, "ゲストに届かなければ前提不足 (不合格と混ぜない)")
        check(rep is None, "報告を作らない")
        check("手が届かない" in "\n".join(log), "届かないと言う [V4]")


def t_run_reopen_fails():
    print("== 15. 通し (rshell が開き直せない) ==", flush=True)
    rep, rc, _, log = drive([], reopen_ok=False)
    eq(rc, gt.RC_NO_PREREQ, "rshell が開き直せなければ非ゼロ")
    check(rep is None, "合格にしない")


def t_run_twice():
    print("== 16. 通し (2 回続けて回す — R8) ==", flush=True)
    with tempfile.TemporaryDirectory(prefix="os32-guest-tests-") as tmp:
        mk = lambda tok: [(0.0, tape(tok, blk("aa", 0, ["aa: PASS 1/1"]),
                                     blk("bb", 0, ["bb: PASS 2/2"]),
                                     blk("cc", 0, ["cc: PASS 3/3"])) + done(tok))]
        rep1, rc1, _, _ = drive(mk("tk1"), token="tk1", tmp=tmp, host_dir=tmp)
        eq(rc1, gt.RC_OK, "1 回目は 0")
        rep2, rc2, _, _ = drive(mk("tk2"), token="tk2", tmp=tmp, host_dir=tmp)
        eq(rc2, gt.RC_OK, "2 回目も同じ結果 (前回の出力が混ざらない)")
        eq([r.kind for r in rep2.rows], [r.kind for r in rep1.rows],
           "2 回目の判定は 1 回目と同じ")

        # 2 回目が**走らなかった**場合、1 回目の出力をそのまま読んで合格にしない
        rep3, rc3, _, log = drive([], token="tk3", tmp=tmp, host_dir=tmp)
        check(rc3 != gt.RC_OK,
              "走らなかった 2 回目が前回の出力で合格にならない (R8)")


def t_report_shape():
    print("== 17. 報告の形 ==", flush=True)
    text = "\n".join(tape("tk", blk("aa", 0, ["aa: PASS 1/1"]),
                          blk("bb", 1, ["bb: FAIL 1/2"]),
                          blk("cc", 139)) + done("tk"))
    rep = gt.aggregate(ENTS, text, "tk")
    body = gt.format_report(rep, ENTS)
    for want in ("aa", "bb", "cc", "PASS", "FAIL", "CRASH", "合計"):
        check(want in body, "報告に %s が出る" % want)
    check("不合格 2 件" in body, "不合格の件数を出す")
    check(body.count("\n") >= len(ENTS) + 4, "1 件 1 行で表になっている")


# ---------------------------------------------------------------------------
#  否定側
# ---------------------------------------------------------------------------

MUTATIONS = [
    # 1. 食い違い (0 なのに FAIL) を見逃す
    ("    if rc == EXIT_PASS:\n"
     "        if summary is None:\n"
     "            return \"mismatch\", \"$?=0 なのに集計行が無い\"",
     "    if rc == EXIT_PASS:\n"
     "        return \"pass\", \"\"\n"
     "        if summary is None:\n"
     "            return \"mismatch\", \"$?=0 なのに集計行が無い\""),
    # 2. 見張りが発火しない
    ("        if now - self.last_growth >= self.stall_secs:\n"
     "            return \"stall\"",
     "        if False:\n"
     "            return \"stall\""),
    # 3. 固まった試験を名指ししない
    ("    return started\n", "    return None\n"),
    # 4. /host が無いのに合格にする (置き場の門)
    ("    if not dir_exists:\n"
     "        return False, (\"HostDrv の置き場がホスト側に無い",
     "    if False:\n"
     "        return False, (\"HostDrv の置き場がホスト側に無い"),
    # 4b. ゲストに /host がマウントされていないのを見逃す (印の門)
    ("    if probe_text is None:\n"
     "        return False, (\"ゲストが %s へ書いた印がホスト側に現れない",
     "    if probe_text is None:\n"
     "        probe_text = token\n"
     "    if False:\n"
     "        return False, (\"ゲストが %s へ書いた印がホスト側に現れない"),
    # 5. 落ちた試験 (139) で後続を打ち切る
    ("        rep.rows.append(Row(e.name, kind, blk.rc, raw, note))\n",
     "        rep.rows.append(Row(e.name, kind, blk.rc, raw, note))\n"
     "        if kind == \"crash\":\n"
     "            break\n"),
    # 6. 前回の出力が混ざるのを見逃す (R8)
    ("    elif token is not None and out.begin_token != token:",
     "    elif False:"),
    # 7. 1 なのに PASS を見逃す
    ("        if summary.kind != \"FAIL\":", "        if False:"),
    # 8. 予約値 (139) を合格として読む
    ("        return RESERVED[rc], \"\"", "        return \"pass\", \"\""),
    # 9. 全体の上限で結果を全部捨てる
    ("    text = read_text(out_host) or \"\"\n"
     "    rep = aggregate(entries, text, token)\n",
     "    text = read_text(out_host) or \"\"\n"
     "    if state == \"timeout\":\n"
     "        log(\"上限\")\n"
     "        return None, RC_STALLED\n"
     "    rep = aggregate(entries, text, token)\n"),
]

MUT_LABELS = [
    "食い違い (0 なのに FAIL) を見逃す",
    "見張りが発火しない",
    "固まった試験を名指ししない",
    "/host が無いのに合格にする (置き場の門)",
    "ゲストに /host が無いのを見逃す (印の門)",
    "落ちた試験 (139) で後続を打ち切る",
    "前回の出力が混ざるのを見逃す (R8)",
    "1 なのに PASS を見逃す",
    "予約値 (139) を合格として読む",
    "全体の上限で結果を全部捨てる",
]


def run_self():
    """今のソースで全部踏む。返り値は落ちた件数。"""
    for fn in (t_parse_list, t_build_script, t_parse_output, t_classify,
               t_aggregate, t_running_test, t_watch, t_hostdrv,
               t_run_all_pass, t_run_crash_continues, t_run_stall,
               t_run_timeout, t_run_no_host, t_run_unreachable,
               t_run_reopen_fails,
               t_run_twice, t_report_shape):
        fn()
    return len(FAILS)


SELF = "tools/tests/test_guest_tests.py"


def one_mutation(item):
    """変異 1 本: 写しの木の tools/guest_tests.py を壊し、写しの中のこの試験を
    --self で流し直す (実物は読むだけ)。(印字, 見逃し) を返す。"""
    i, ((old, new), label) = item
    tag = "%d %s" % (i, label)
    original = TARGET.read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-44s SKIP (目印が見つからない)" % tag, 1
    with tempfile.TemporaryDirectory(prefix="os32-guest-tests-mut-") as td:
        p = mutpar.run_script_in_tree(
            ROOT, td, {str(TARGET.relative_to(ROOT)):
                       original.replace(old, new, 1)},
            SELF, ["--self"], capture_output=True, timeout=300)
    if p.returncode == 0:
        return ("MUTATE %-44s **GREEN のまま = 試験が規則を見ていない**" % tag,
                1)
    n = p.stdout.decode("utf-8", "replace").count("  FAIL ")
    # 例外で死んだだけ / 構文が壊れただけは**検出に数えない**。
    # 試験の目が規則を見ていた証拠は「検査が落ちたこと」だけ。
    if n == 0:
        return ("MUTATE %-44s **検査が 1 つも落ちていない "
                "(落ちただけ) = 目が働いていない**" % tag, 1)
    return "MUTATE %-44s RED (期待どおり落ちた: %d 件)" % (tag, n), 0


def run_mutations():
    """実物の tools/guest_tests.py の写しを 1 か所ずつ壊し、RED になるか見る
    (変異は一時ディレクトリの写しにだけ当てる。mutpar で並列、check-par で回せる)。"""
    return mutpar.run_with_control(
        one_mutation, list(enumerate(zip(MUTATIONS, MUT_LABELS), 1)),
        (0, (("", ""), "control")))


if __name__ == "__main__":
    if "--self" in sys.argv:
        sys.exit(1 if run_self() else 0)

    failed = run_self()
    print("HOST %d checks failed" % failed, flush=True)
    if "--mutate" in sys.argv:
        print("", flush=True)
        failed += run_mutations()
    sys.exit(1 if failed else 0)
