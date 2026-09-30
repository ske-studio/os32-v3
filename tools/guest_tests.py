#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""guest_tests.py — 所定の試験をゲストで一括実行し、ホストで機械が集計する。

票: docs/archive/test/TASK_TEST_RUNNER.md (ランナー 3 段目)
前段: docs/archive/test/TASK_TEST_RESULT.md — 終了コード (0/1/2) と集計行
      `<名前>: PASS <n>/<m>` の約束事。**このランナーはその両方を見て、
      食い違いを不合格として報告する。片方だけ見ると約束事が壊れても誰も
      気づけない。**
記録: tools/tests/guest_tests_tdd.md

## なぜこの形か

OS32 のシェルには **ループも `;` の連結も無い** (票 §1、実測)。あるのは
リダイレクト・`source`・`$?`・1 行形の `if` だけ。だから

  * **ホストが試験ごとに 3 行を並べた平らなスクリプトを組み立て**、
  * ゲストは `source` でそれを頭から流し、
  * 出力は**すべてファイルへ落とす** (`/api/cmd` の応答に乗るのは 1 行目の
    出力だけで、`source` の出力は乗らない)、
  * ホストはファイルを見張って集計する。

## 割り方 — 純粋な部分と触る部分 (票 §5 の R7)

**生成と集計はエミュレータにも時計にも触らない。** そこが壊れていたら
ゲストで回しても意味がないので、贋物の入力だけで全部試験できるようにして
ある (tools/tests/test_guest_tests.py)。

  純粋 (この上半分)   parse_list / build_script / parse_output / classify /
                      aggregate / format_report / Watch / running_test /
                      hostdrv_verdict
  触る (この下半分)   Np21Guest (HTTP)、ファイルの見張り、rshell の開き直し

触る側は薄く保ち、`run_suite()` に **guest / now / sleep を注入できる**形に
してある。試験は贋のゲストを渡して同じ道を通す。

## 使い方

    python3 tools/guest_tests.py --dry-run      # 生成するスクリプトを出すだけ
    python3 tools/guest_tests.py --list         # 一覧を読んで並びを出すだけ
    python3 tools/guest_tests.py                # ゲストで回して集計する
    python3 tools/guest_tests.py --stall 180 --total 3600

`--dry-run` と `--list` は **NP21/W に一切触れない**。

## 終了コード

    0  全件が合格 (SKIP は不合格に数えない — 票 §2-4 の「実行しなかった」)
    1  使い方・一覧の誤り
    2  不合格または食い違いがあった
    3  固まった / 全体の上限に当たった (**そこまでの結果は出してある**)
    4  前提が無い (/host が使えない、ゲストに手が届かない) — 合格にしない [V4]
"""

import argparse
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ_DIR = os.path.dirname(HERE)

# ---------------------------------------------------------------------------
#  定数 — [C4] 直書きしない。意味のある名前を 1 か所に置く
# ---------------------------------------------------------------------------

#: 一覧の正典 (票 §2-3 — 走らせるものは票ではなくファイルに持つ)
LIST_REL = "tools/tests/guest_tests.txt"

#: ゲストから見た作業場所。ホスト側の <HOSTDRV_DIR>/test/ に同じものが見える。
GUEST_DIR = "/host/test"
GUEST_OUT = GUEST_DIR + "/out.txt"
GUEST_SCRIPT = GUEST_DIR + "/runtests.sh"
GUEST_PROBE = GUEST_DIR + "/probe.txt"
HOST_SUBDIR = "test"
HOST_OUT_NAME = "out.txt"
HOST_SCRIPT_NAME = "runtests.sh"
HOST_PROBE_NAME = "probe.txt"

#: 出力ファイルの目印。ゲストの `echo` が書き、ホストの parse_output が読む。
MARK_SUITE = "RUNTESTS"
MARK_BEGIN = "BEGIN"
MARK_DONE = "DONE"
MARK_RUN = "RUN"
MARK_RC = "RC"

#: 終了コード (票 §2-1 / TASK_TEST_RESULT.md §2-1)
EXIT_PASS = 0
EXIT_FAIL = 1
EXIT_SKIP = 2
LOCAL_MIN = 3
LOCAL_MAX = 125
#: シェルの予約値。試験は返さない = 返っていたら「落ちた・起こせなかった」。
RESV_SPAWN = 126
RESV_NOTFOUND = 127
RESV_INTR = 130
RESV_FAULT = 139
RESERVED = {
    RESV_SPAWN: "nostart",
    RESV_NOTFOUND: "notfound",
    RESV_INTR: "intr",
    RESV_FAULT: "crash",
}

#: 見張りの時間 ([V3] 短く切らない)。1 本あたり 60 秒を下回る指定は断る。
STALL_SECS_MIN = 60
STALL_SECS_DEFAULT = 180
TOTAL_SECS_DEFAULT = 3600
POLL_EVERY_DEFAULT = 5

#: HTTP の待ち時間 ([V3] 15 秒未満にしない)
T_SHORT = 20
T_CMD = 60
#: `source` を投げるときの待ち。**時間切れは失敗ではない** (走り続けている)。
T_SOURCE = 30

#: ランナー自身の終了コード
RC_OK = 0
RC_USAGE = 1
RC_TESTS_FAILED = 2
RC_STALLED = 3
RC_NO_PREREQ = 4

#: 名前は目印と正規表現に使うので、字種を絞る。
NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

#: 判定の種別 → (人が読む語, 合格に数えるか)
KINDS = {
    "pass": ("PASS", True),
    "skip": ("SKIP", True),
    "fail": ("FAIL", False),
    "crash": ("CRASH", False),
    "nostart": ("NOSTART", False),
    "notfound": ("NOTFOUND", False),
    "intr": ("INTR", False),
    "local": ("RC%d" % LOCAL_MIN, False),
    "mismatch": ("MISMATCH", False),
    "incomplete": ("INCOMPLETE", False),
    "missing": ("MISSING", False),
}


# ===========================================================================
#  純粋な部分 — エミュレータにも時計にもファイルにも触らない
# ===========================================================================

class Entry(object):
    """一覧の 1 行。name は集計行の名前、cmd はゲストに打たせる 1 行。"""

    __slots__ = ("name", "cmd", "line_no")

    def __init__(self, name, cmd, line_no):
        self.name = name
        self.cmd = cmd
        self.line_no = line_no

    def __repr__(self):
        return "Entry(%r, %r)" % (self.name, self.cmd)


def parse_list(text):
    """一覧のテキストを Entry の並びにする。返り値: (entries, errors)

    1 行 1 件。`#` から行末はコメント、空行は無視。行の**最初の語**が名前で、
    行全体がゲストに打たせる命令 (`restest all` → 名前 `restest`)。

    名前が重なると出力の目印が区別できなくなるので**重複は誤り**にする。
    """
    entries = []
    errors = []
    seen = {}
    for i, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        name = line.split()[0]
        if not NAME_RE.match(name):
            errors.append("%d 行目: 名前に使えない字がある: %r" % (i, name))
            continue
        if name in seen:
            errors.append("%d 行目: 名前 %s が %d 行目と重なっている"
                          % (i, name, seen[name]))
            continue
        seen[name] = i
        entries.append(Entry(name, line, i))
    if not entries and not errors:
        errors.append("一覧が空 — 走らせるものが 1 件も無い")
    return entries, errors


def build_script(entries, token, out_path=GUEST_OUT):
    """ゲストで `source` する平らなスクリプトを組み立てる (票 §2-1)。

    ループが無いので**試験ごとに 3 行**を並べる。先頭の 1 行だけ `>` で
    出力ファイルを切り詰める — 前回の出力が混ざらないため (受入 R8)。
    token は「この出力は今回のものか」をホストが確かめるための合い言葉。

    **どの試験も無条件に並べる。** 前の試験が落ちても次の行は実行される
    (票 §1 の実測) ので、1 本の事故で後続が消えない (受入 R3)。
    """
    lines = ["echo %s %s %s > %s" % (MARK_SUITE, MARK_BEGIN, token, out_path)]
    for e in entries:
        lines.append("echo %s %s >> %s" % (MARK_RUN, e.name, out_path))
        lines.append("%s >> %s" % (e.cmd, out_path))
        lines.append("echo %s %s $? >> %s" % (MARK_RC, e.name, out_path))
    lines.append("echo %s %s %s >> %s"
                 % (MARK_SUITE, MARK_DONE, token, out_path))
    return "\n".join(lines) + "\n"


class Summary(object):
    """集計行 1 行 (`<名前>: PASS 3/3` など) を解いたもの。"""

    __slots__ = ("kind", "passed", "total", "reason", "raw")

    def __init__(self, kind, passed=None, total=None, reason=None, raw=""):
        self.kind = kind            # "PASS" / "FAIL" / "SKIP"
        self.passed = passed
        self.total = total
        self.reason = reason
        self.raw = raw

    def __repr__(self):
        return "Summary(%r, %r, %r, %r)" % (self.kind, self.passed,
                                            self.total, self.reason)


def parse_summary(name, line):
    """その試験の集計行なら Summary を返す。違えば None。

    名前は**固定文字列**の約束 (TASK_TEST_RESULT.md §2-2) なので、
    行頭が `<名前>: ` でないものは他人の行として見ない。
    """
    head = name + ": "
    if not line.startswith(head):
        return None
    rest = line[len(head):].strip()
    m = re.match(r"^(PASS|FAIL)\s+(\d+)\s*/\s*(\d+)$", rest)
    if m:
        return Summary(m.group(1), int(m.group(2)), int(m.group(3)), None, line)
    if rest.startswith("SKIP"):
        reason = rest[len("SKIP"):].strip()
        return Summary("SKIP", None, None, reason, line)
    return None


class Block(object):
    """出力ファイルの中の 1 試験ぶん (`RUN x` 〜 `RC x n`)。"""

    __slots__ = ("name", "rc", "rc_raw", "summary", "body", "order", "closed")

    def __init__(self, name, order):
        self.name = name
        self.order = order
        self.rc = None
        self.rc_raw = None
        self.summary = None
        self.body = []
        self.closed = False


class Output(object):
    """出力ファイル全体を解いたもの。"""

    __slots__ = ("begin_token", "done_token", "blocks", "order", "repeats")

    def __init__(self):
        self.begin_token = None
        self.done_token = None
        self.blocks = {}
        self.order = []
        self.repeats = []       # 同じ名前の RUN が 2 回出た


def parse_output(text):
    """出力テキストを Output にする。**時計にもファイルにも触らない。**"""
    out = Output()
    cur = None
    for raw in text.splitlines():
        line = raw.replace("\r", "").rstrip("\n")
        s = line.strip()
        parts = s.split()
        if len(parts) >= 2 and parts[0] == MARK_SUITE:
            if parts[1] == MARK_BEGIN:
                out.begin_token = parts[2] if len(parts) > 2 else ""
                continue
            if parts[1] == MARK_DONE:
                out.done_token = parts[2] if len(parts) > 2 else ""
                cur = None
                continue
        if len(parts) == 2 and parts[0] == MARK_RUN:
            name = parts[1]
            if name in out.blocks:
                out.repeats.append(name)
                cur = out.blocks[name]
                continue
            cur = Block(name, len(out.order))
            out.blocks[name] = cur
            out.order.append(name)
            continue
        if len(parts) >= 2 and parts[0] == MARK_RC:
            name = parts[1]
            blk = out.blocks.get(name)
            if blk is not None:
                blk.closed = True
                blk.rc_raw = parts[2] if len(parts) > 2 else ""
                try:
                    blk.rc = int(blk.rc_raw)
                except ValueError:
                    blk.rc = None
            cur = None
            continue
        if cur is not None:
            cur.body.append(line)
            got = parse_summary(cur.name, s)
            if got is not None:
                cur.summary = got       # 最後に出たものを採る (約束は最終行)
    return out


def classify(rc, summary):
    """終了コードと集計行の**両方**から判定する (票 §2-4)。

    返り値: (kind, note)。片方を選ばない — `0` なのに `FAIL`、`1` なのに
    `PASS` は **食い違い = 不合格**。ランナーが片方だけ見ると、約束事が
    壊れたときに誰も気づけない。
    """
    if rc is None:
        return "incomplete", "終了コードの行が無い (途中で切れた)"

    if rc in RESERVED:
        # 予約値は「落ちた・起こせなかった」。集計行が**出ないのが正常**で、
        # これを「集計行が無い = 異常」と一緒くたにしない (票 §2-4)。
        if summary is not None:
            return "mismatch", ("$?=%d (落ちた値) なのに集計行 %r がある"
                                % (rc, summary.raw.strip()))
        return RESERVED[rc], ""

    if rc == EXIT_PASS:
        if summary is None:
            return "mismatch", "$?=0 なのに集計行が無い"
        if summary.kind != "PASS":
            return "mismatch", ("$?=0 なのに集計行が %s (%s)"
                                % (summary.kind, summary.raw.strip()))
        if not (summary.total > 0 and summary.passed == summary.total):
            return "mismatch", ("$?=0 で PASS なのに数が釣り合わない (%s)"
                                % summary.raw.strip())
        return "pass", ""

    if rc == EXIT_FAIL:
        if summary is None:
            return "mismatch", "$?=1 なのに集計行が無い"
        if summary.kind != "FAIL":
            return "mismatch", ("$?=1 なのに集計行が %s (%s)"
                                % (summary.kind, summary.raw.strip()))
        if summary.total is not None and summary.total > 0 \
                and summary.passed == summary.total:
            return "mismatch", ("$?=1 で FAIL なのに全件合格の数 (%s)"
                                % summary.raw.strip())
        return "fail", summary.raw.strip()

    if rc == EXIT_SKIP:
        if summary is None:
            return "mismatch", "$?=2 なのに集計行が無い"
        if summary.kind != "SKIP":
            return "mismatch", ("$?=2 なのに集計行が %s (%s)"
                                % (summary.kind, summary.raw.strip()))
        return "skip", summary.reason or ""

    if LOCAL_MIN <= rc <= LOCAL_MAX:
        return "local", "票が定義した個別の終了コード %d" % rc

    return "mismatch", "約束事に無い終了コード %d" % rc


class Row(object):
    """報告の 1 行。"""

    __slots__ = ("name", "kind", "rc", "summary", "note")

    def __init__(self, name, kind, rc, summary, note):
        self.name = name
        self.kind = kind
        self.rc = rc
        self.summary = summary
        self.note = note

    @property
    def ok(self):
        return KINDS[self.kind][1]

    @property
    def label(self):
        if self.kind == "local":
            return "RC%s" % self.rc
        return KINDS[self.kind][0]


class Report(object):
    __slots__ = ("rows", "problems", "done", "stalled", "stalled_at",
                 "truncated")

    def __init__(self):
        self.rows = []
        self.problems = []      # 全体の問題 (合い言葉のずれ・重複など)
        self.done = False
        self.stalled = False    # 出力が伸びなくなった
        self.stalled_at = None  # そのとき走っていた試験の名前 (分かれば)
        self.truncated = False  # 全体の上限に当たった

    @property
    def failed(self):
        return [r for r in self.rows if not r.ok]

    @property
    def counts(self):
        c = {}
        for r in self.rows:
            c[r.kind] = c.get(r.kind, 0) + 1
        return c

    @property
    def ok(self):
        return (not self.failed) and (not self.problems) and self.done \
            and not self.stalled and not self.truncated


def aggregate(entries, text, token):
    """一覧と出力テキストから報告を組み立てる (票 §2-4)。

    **一覧の全件を必ず 1 行ずつ出す。** 途中の 1 本が落ちても打ち切らない —
    落ちた試験の後ろも走っている (票 §1 の実測) ので、集計も最後まで行く。
    """
    out = parse_output(text)
    rep = Report()

    if out.begin_token is None:
        rep.problems.append("出力に `%s %s` が無い — スクリプトが走り出して "
                            "いない可能性がある" % (MARK_SUITE, MARK_BEGIN))
    elif token is not None and out.begin_token != token:
        rep.problems.append(
            "出力の合い言葉が違う (%r、今回は %r) — **前回の出力が残っている**"
            % (out.begin_token, token))
    if out.done_token is not None:
        if token is not None and out.done_token != token:
            rep.problems.append("完了印の合い言葉が違う (%r)" % out.done_token)
        else:
            rep.done = True
    for name in out.repeats:
        rep.problems.append("%s の `%s` 行が 2 回出た — 出力が混ざっている"
                            % (name, MARK_RUN))

    listed = set()
    for e in entries:
        listed.add(e.name)
        blk = out.blocks.get(e.name)
        if blk is None:
            rep.rows.append(Row(e.name, "missing", None, None,
                                "出力に `%s %s` が無い (走っていない)"
                                % (MARK_RUN, e.name)))
            continue
        kind, note = classify(blk.rc, blk.summary)
        raw = blk.summary.raw.strip() if blk.summary is not None else ""
        rep.rows.append(Row(e.name, kind, blk.rc, raw, note))

    for name in out.order:
        if name not in listed:
            rep.problems.append("一覧に無い試験 %s の結果が出力にある" % name)

    return rep


def running_test(text):
    """**どれで止まったか。** 最後に始まって終わっていない試験の名前。

    「止まりました」だけでは次に何をすればいいか分からない (票 §3)。
    全部終わっていれば None。
    """
    started = None
    for raw in text.splitlines():
        parts = raw.replace("\r", "").strip().split()
        if len(parts) == 2 and parts[0] == MARK_RUN:
            started = parts[1]
        elif len(parts) >= 2 and parts[0] == MARK_RC and parts[1] == started:
            started = None
        elif len(parts) >= 2 and parts[0] == MARK_SUITE \
                and parts[1] == MARK_DONE:
            started = None
    return started


class Watch(object):
    """出力が伸びているかの見張り。**時計を持たない** — now を渡す。

    伸びなくなったら "stall"、全体の上限に当たったら "timeout"。
    どちらも「そこまでの結果を出してから」落ちるために、呼び手へ種別だけ返す。
    """

    def __init__(self, stall_secs=STALL_SECS_DEFAULT,
                 total_secs=TOTAL_SECS_DEFAULT):
        # [V3] 待ち時間を短く切らない。1 本あたりの見張りは 60 秒以上から。
        if stall_secs < STALL_SECS_MIN:
            raise ValueError("1 本あたりの見張りは %d 秒以上にすること [V3] "
                             "(指定: %s)" % (STALL_SECS_MIN, stall_secs))
        if total_secs <= stall_secs:
            raise ValueError("全体の上限は 1 本あたりの見張りより長くすること "
                             "(全体 %s / 1 本 %s)" % (total_secs, stall_secs))
        self.stall_secs = stall_secs
        self.total_secs = total_secs
        self.started = None
        self.last_growth = None
        self.last_size = -1

    def start(self, now):
        self.started = now
        self.last_growth = now
        self.last_size = -1
        return self

    def observe(self, now, size):
        """返り値: "run" / "stall" / "timeout"。"""
        if self.started is None:
            self.start(now)
        if size > self.last_size:
            self.last_size = size
            self.last_growth = now
        if now - self.last_growth >= self.stall_secs:
            return "stall"
        if now - self.started >= self.total_secs:
            return "timeout"
        return "run"


def hostdrv_verdict(dir_exists, probe_text, token):
    """`/host` が本当に使えるか。返り値: (ok, 理由)

    **黙って合格にしない** ([V4])。実機には HostDrv が無く、そこでこの
    ランナーを回しても結果を持ち帰る道が無い (票 §2-2 の 1)。
    """
    if not dir_exists:
        return False, ("HostDrv の置き場がホスト側に無い — NP21/W の HostDrv "
                       "が有効か、HOSTDRV_DIR が正しいか確かめること")
    if probe_text is None:
        return False, ("ゲストが %s へ書いた印がホスト側に現れない — "
                       "ゲストに /host がマウントされていない "
                       "(実機には HostDrv が無い)" % GUEST_PROBE)
    if token not in probe_text:
        return False, ("%s の中身が今回の印と違う (%r) — 別の経路の残骸か、"
                       "書き込みが届いていない" % (GUEST_PROBE, probe_text[:80]))
    return True, ""


def format_report(rep, entries):
    """報告を人が読む表にする。**確かめていないことも書く** ([V4])。"""
    lines = []
    width = max([len(e.name) for e in entries] + [4])
    lines.append("")
    lines.append("%-*s  %-10s %-5s %s" % (width, "試験", "判定", "$?", "集計行"))
    lines.append("-" * (width + 32))
    for r in rep.rows:
        rc = "-" if r.rc is None else str(r.rc)
        detail = r.summary or r.note
        lines.append("%-*s  %-10s %-5s %s" % (width, r.name, r.label, rc, detail))
    lines.append("-" * (width + 32))

    c = rep.counts
    order = ["pass", "skip", "fail", "mismatch", "crash", "nostart",
             "notfound", "intr", "local", "incomplete", "missing"]
    tally = ", ".join("%s=%d" % (KINDS[k][0] if k != "local" else "RC3-125",
                                 c[k]) for k in order if k in c)
    lines.append("合計 %d 件: %s" % (len(rep.rows), tally or "なし"))

    for p in rep.problems:
        lines.append("問題: %s" % p)
    if rep.stalled:
        lines.append("固まった試験: **%s** — ここで出力が伸びなくなった"
                     % (rep.stalled_at or "(どれか分からない)"))
    if rep.truncated:
        lines.append("全体の上限に当たった (ここまでの結果を出してある)")
    if not rep.done and not rep.stalled and not rep.truncated:
        lines.append("完了印 (`%s %s`) が出ていない — 最後まで走っていない"
                     % (MARK_SUITE, MARK_DONE))

    bad = rep.failed
    if bad:
        lines.append("")
        lines.append("不合格 %d 件:" % len(bad))
        for r in bad:
            lines.append("  %-*s %-10s %s"
                         % (width, r.name, r.label, r.summary or r.note))
    return "\n".join(lines) + "\n"


def report_exit_code(rep):
    """報告からランナーの終了コードを決める。"""
    if rep.stalled or rep.truncated:
        return RC_STALLED
    if rep.failed or rep.problems or not rep.done:
        return RC_TESTS_FAILED
    return RC_OK


# ===========================================================================
#  触る部分 — ここから下だけが HTTP とファイルに触る。薄く保つこと。
# ===========================================================================

class GuestUnreachable(Exception):
    """ゲストに手が届かない (NP21/W が動いていない / aidebug が無効 など)。

    **これを「試験が不合格」と混ぜない。** 走らせていないのだから合否は
    まだ無い ([V4])。
    """


def make_token(now=None):
    """今回の実行の合い言葉。前回の出力と見分けるためだけに使う。"""
    t = time.time() if now is None else now
    return "gt%d" % (int(t * 1000) % 1000000000)


def resolve_hostdrv_dir():
    """HostDrv の置き場。解決は tools/hostdrv_deploy.py が正典なので借りる。"""
    if os.environ.get("HOSTDRV_DIR"):
        return os.environ["HOSTDRV_DIR"]
    sys.path.insert(0, HERE)
    try:
        import hostdrv_deploy
        return hostdrv_deploy.HOSTDRV_DIR
    except Exception:
        return "/mnt/c/os32"


class Np21Guest(object):
    """動いているゲストへ手を届かせる薄い包み。

    HTTP は tools/np21w_mcp/np21w_client.py が既にあるので作り直さない
    (WSL からの直結と Windows の curl.exe への切り替えを持っている)。
    """

    def __init__(self, client=None):
        if client is None:
            sys.path.insert(0, os.path.join(HERE, "np21w_mcp"))
            import np21w_client
            client = np21w_client
        self.client = client

    def cmd(self, line, timeout=T_CMD):
        """シェルに 1 行送る。**時間切れは失敗ではない** — 走り続けている。

        時間切れ以外の失敗 (繋がらない・接続が切れた) は届いていないので、
        黙って空文字を返さず GuestUnreachable にする。
        """
        try:
            return self.client.post("/api/cmd", line, timeout=timeout) \
                .decode("utf-8", "replace")
        except Exception as exc:
            if "timed out" in str(exc) or "time out" in str(exc):
                return ""
            raise GuestUnreachable("%s (%s)" % (exc, line.split()[0]))

    def key(self, seq=None, text=None):
        import urllib.parse
        if text is not None:
            # raw リングが浅いので 4 文字ずつ (tools/gui_gate.py の実測)。
            i = 0
            while i < len(text):
                self.client.post("/api/key",
                                 urllib.parse.urlencode({"text": text[i:i + 4]}),
                                 timeout=T_SHORT)
                time.sleep(0.35)
                i += 4
        if seq is not None:
            self.client.post("/api/key", urllib.parse.urlencode({"seq": seq}),
                             timeout=T_SHORT)

    def reopen_shell(self):
        """rshell を開き直す (票 §2-2 の 2)。

        長く開いたままのセッションは腐る — `$?` が固着し `source` が効かなく
        なる (2026-09-17 実測。.claude/skills/run-os32/driver.py の注記は
        まさにこの腐った状態を書いたもの)。

        **必ず ESC を先に送ってから `rshell` を打つ。** 生きたまま打つと
        rshell が 2 段に重なり、以降の ESC が内側しか閉じない
        (tools/gui_gate.py の 2026-09-11 の教訓)。
        """
        self.key(seq="ESC")
        time.sleep(1.0)
        self.key(text="rshell")
        self.key(seq="RETURN")
        time.sleep(2.0)
        out = self.cmd("ver", timeout=T_SHORT)
        return "OS32" in out


def read_text(path):
    """開いてみる。**存在確認ではなく開くこと** — /mnt/c 側は一覧が遅れる
    (.claude/skills/run-os32/driver.py の実測)。"""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return None


def file_size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return -1


def run_suite(guest, host_dir, entries, stall_secs=STALL_SECS_DEFAULT,
              total_secs=TOTAL_SECS_DEFAULT, poll_every=POLL_EVERY_DEFAULT,
              token=None, now=None, sleep=None, log=None):
    """ゲストで一括実行してホストで集計する。返り値: (Report, 終了コード)

    guest / now / sleep は**注入できる** — 試験は贋のゲストと贋の時計を渡して
    同じ道を通る (票 §5 の R7)。
    """
    now = now or time.monotonic
    sleep = sleep or time.sleep
    log = log or (lambda s: sys.stdout.write(s + "\n"))
    token = token or make_token()

    test_dir = os.path.join(host_dir, HOST_SUBDIR)
    out_host = os.path.join(test_dir, HOST_OUT_NAME)
    script_host = os.path.join(test_dir, HOST_SCRIPT_NAME)
    probe_host = os.path.join(test_dir, HOST_PROBE_NAME)

    # --- 1. /host が使えるか (票 §2-2 の 1)。黙って合格にしない [V4] ---
    dir_exists = os.path.isdir(host_dir)
    if dir_exists:
        try:
            os.makedirs(test_dir, exist_ok=True)
        except OSError:
            dir_exists = False
    for p in (out_host, probe_host):
        try:
            os.remove(p)                # R8: 前回の出力を混ぜない
        except OSError:
            pass
    try:
        if dir_exists:
            guest.cmd("echo %s > %s" % (token, GUEST_PROBE), timeout=T_SHORT)
    except GuestUnreachable as exc:
        log("guest_tests: ゲストに手が届かない — %s。NP21/W が動いていて "
            "aidebug=true か確かめること" % exc)
        return None, RC_NO_PREREQ
    ok, why = hostdrv_verdict(dir_exists, read_text(probe_host), token)
    if not ok:
        log("guest_tests: /host が使えない — %s" % why)
        return None, RC_NO_PREREQ

    # --- 2. rshell を開き直す (腐ったセッションでは source が効かない) ---
    try:
        reopened = guest.reopen_shell()
    except GuestUnreachable as exc:
        log("guest_tests: ゲストに手が届かない — %s" % exc)
        return None, RC_NO_PREREQ
    if not reopened:
        log("guest_tests: rshell を開き直せなかった (`ver` が返らない)")
        return None, RC_NO_PREREQ

    # --- 3. スクリプトを生成して /host へ置く (どの順で走るかがホストに残る) ---
    script = build_script(entries, token)
    with open(script_host, "w", encoding="utf-8", newline="\n") as f:
        f.write(script)
    log("guest_tests: %d 件、合い言葉 %s、%s" % (len(entries), token, script_host))

    # --- 4. source を投げる。応答の時間切れは失敗ではない ---
    try:
        guest.cmd("source %s" % GUEST_SCRIPT, timeout=T_SOURCE)
    except GuestUnreachable as exc:
        log("guest_tests: source を投げられなかった — %s" % exc)
        return None, RC_NO_PREREQ

    # --- 5. 出力ファイルを見張る (票 §3) ---
    watch = Watch(stall_secs, total_secs).start(now())
    state = "run"
    while True:
        text = read_text(out_host) or ""
        if parse_output(text).done_token is not None:
            break
        state = watch.observe(now(), file_size(out_host))
        if state != "run":
            break
        sleep(poll_every)

    # --- 6. 集計。上限に当たっても**そこまでの結果を出してから**落ちる ---
    text = read_text(out_host) or ""
    rep = aggregate(entries, text, token)
    if state == "stall":
        rep.stalled = True
        rep.stalled_at = running_test(text)
    elif state == "timeout":
        rep.truncated = True
        rep.stalled_at = running_test(text)
    log(format_report(rep, entries))
    if state == "stall":
        log("guest_tests: 出力が %d 秒伸びなかった。固まったのは %s"
            % (stall_secs, rep.stalled_at or "(どれか分からない)"))
    elif state == "timeout":
        log("guest_tests: 全体の上限 %d 秒に当たった" % total_secs)
    return rep, report_exit_code(rep)


# ===========================================================================
#  入口
# ===========================================================================

def load_entries(path):
    text = read_text(path)
    if text is None:
        return None, ["一覧が読めない: %s" % path]
    return parse_list(text)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="所定の試験をゲストで一括実行し、ホストで集計する "
                    "(票 docs/archive/test/TASK_TEST_RUNNER.md)")
    ap.add_argument("--list-file", default=os.path.join(PROJ_DIR, LIST_REL),
                    help="走らせる一覧 (既定: %s)" % LIST_REL)
    ap.add_argument("--dry-run", action="store_true",
                    help="生成するスクリプトを出すだけ (NP21/W に触れない)")
    ap.add_argument("--list", action="store_true",
                    help="一覧を読んで並びを出すだけ (NP21/W に触れない)")
    ap.add_argument("--stall", type=int, default=STALL_SECS_DEFAULT,
                    help="1 本あたりの見張り秒数 (%d 以上 [V3])" % STALL_SECS_MIN)
    ap.add_argument("--total", type=int, default=TOTAL_SECS_DEFAULT,
                    help="全体の上限秒数")
    ap.add_argument("--poll", type=int, default=POLL_EVERY_DEFAULT,
                    help="見張りの間隔 (秒)")
    args = ap.parse_args(argv)

    entries, errors = load_entries(args.list_file)
    for e in errors:
        sys.stderr.write("guest_tests: %s\n" % e)
    if errors:
        return RC_USAGE

    if args.list:
        for e in entries:
            sys.stdout.write("%-16s %s\n" % (e.name, e.cmd))
        return RC_OK
    if args.dry_run:
        sys.stdout.write(build_script(entries, make_token()))
        return RC_OK

    try:
        Watch(args.stall, args.total)
    except ValueError as exc:
        sys.stderr.write("guest_tests: %s\n" % exc)
        return RC_USAGE

    try:
        guest = Np21Guest()
    except Exception as exc:
        sys.stderr.write("guest_tests: ゲストに手が届かない (%s)。NP21/W が "
                         "動いているか確かめること\n" % exc)
        return RC_NO_PREREQ
    _, rc = run_suite(guest, resolve_hostdrv_dir(), entries,
                      stall_secs=args.stall, total_secs=args.total,
                      poll_every=args.poll)
    return rc


if __name__ == "__main__":
    sys.exit(main())
