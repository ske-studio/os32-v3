"""rshell_serial.py の「応答の識別」のホスト試験。

記録: tools/tests/serial_vfast_tdd.md
票  : docs/archive/realhw_v21/TASK_SERIAL_VFAST.md (Codex レビュー往復 3 ⑤⑥)

実物の tools/rshell_serial.py を import して、純粋な判定だけを回す
(pyserial もシリアルポートも要らない — モジュールは pyserial が無くても
import できるようにしてある)。

**ここはエミュレータでも実機でも「たまたま通る」**。EOT の取り違えは
タイミングで起きるので、再現を待っていては直したかどうか分からない。
だから識別の規則そのものをここで固定する。

  python3 -B tools/tests/test_rshell_serial.py            # 全ケース
  python3 -B tools/tests/test_rshell_serial.py --mutate   # 否定側
"""
import importlib.util
import inspect
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "tools/rshell_serial.py"


def load(path=None):
    """実物 (または変異させた写し) を import して返す。"""
    spec = importlib.util.spec_from_file_location(
        "rshell_serial_under_test", str(path or SRC))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


FAILED = []


def check(cond, what):
    if not cond:
        FAILED.append(what)


# ---------------------------------------------------------------------------
#  (a) エコー行は **行全体** で比べる (⑥)
# ---------------------------------------------------------------------------
def case_echo(r):
    # 素直な成功。
    check(r.check_echo("> ver\nOS32 v1.4\n  Build: Sep 22 2026\n", "ver"),
          "echo: plain match")
    # 先頭の改行は読み飛ばす (前のコマンドの残りの改行)。
    check(r.check_echo("\r\n> ver\nOS32\n", "ver"), "echo: leading newlines")
    check(r.check_echo("> ver\r\nOS32\n", "ver"), "echo: CRLF")

    # **前方一致では通ってしまう組み合わせ。** `startswith("> ver")` だと
    # `> version` も `> ver foo` も通る = 別のコマンドの応答を読んでいるのに
    # 気づけない (⑥)。
    check(not r.check_echo("> version\nOS32\n", "ver"),
          "echo: '> version' is NOT an echo of 'ver'")
    check(not r.check_echo("> ver foo\nOS32\n", "ver"),
          "echo: '> ver foo' is NOT an echo of 'ver'")
    check(not r.check_echo("> verbose\n", "ver"), "echo: '> verbose' rejected")

    # 別のコマンドの応答 (1 つずれている)。
    check(not r.check_echo("> serial 115200\nRS-232C init\n", "ver"),
          "echo: previous command's reply rejected")
    # エコーが無い (EOT だけ拾った)。
    check(not r.check_echo("  Build: Sep 22 2026\n", "ver"),
          "echo: body without echo rejected")
    check(not r.check_echo("", "ver"), "echo: empty rejected")

    # **`exit` はエコーを出さない** — ゲストは kprintf より先に break する
    # (userland/shell/rshell.c)。ここを desync と誤判定していた (⑥)。
    check(r.check_echo("", "exit"), "echo: exit needs no echo")
    check(r.check_echo("anything at all\n", "exit"), "echo: exit always ok")
    check("exit" in r.NO_ECHO_CMDS, "echo: exit is listed as echo-less")

    # 引数つきのコマンドも行全体で一致すること。
    check(r.check_echo("> serial 9600\nRS-232C init\n", "serial 9600"),
          "echo: command with args")
    check(not r.check_echo("> serial 9600 extra\n", "serial 9600"),
          "echo: extra args rejected")


# ---------------------------------------------------------------------------
#  (b) 切替の確認は `serial ack` の応答で行う (往復 4)
# ---------------------------------------------------------------------------
class Runaway(BaseException):
    """止まらなくなった実装を止める合図。

    **`Exception` から派生させない。** `wait_ack` は送信を
    `except Exception:` で包んでいる (速度不一致で化けたときに落ちないため) ので、
    普通の例外はそこに吸われて無限ループが続いてしまう。
    """


class FakePort:
    """`send_cmd` が使う分だけを真似たポート。

    `replies` は「1 回の send_cmd で返る (本文, EOT を見たか)」の列。
    wait_ack が **投げ直す** ことと、**ACK が来るまで読み飛ばす** ことを
    ここで観察する。

    ⚠ 送信回数に上限を置いて**例外で止める**。置かないと「期限で諦める」を
    壊した変異 (MUTATIONS) が無限ループになり、試験そのものが返らなくなる。
    上限に当たること自体が「諦めていない」という失敗の印。
    """

    SEND_LIMIT = 50

    def __init__(self, replies):
        self.replies = list(replies)
        self.sent = []

    def reset_input_buffer(self):
        pass

    def write(self, data):
        if len(self.sent) >= self.SEND_LIMIT:
            raise Runaway("wait_ack never gave up (%d sends)"
                          % self.SEND_LIMIT)
        self.sent.append(data.decode("ascii", errors="replace"))

    def flush(self):
        pass

    def read(self, _n):          # pragma: no cover — send_cmd を差し替える
        return b""


def fake_send_cmd(port, line, timeout_s):
    port.write((line + "\n").encode("utf-8"))
    if not port.replies:
        return "", False
    return port.replies.pop(0)


def with_fake_send(r, fn):
    """send_cmd だけ差し替えて fn を回す。"""
    real = r.send_cmd
    r.send_cmd = fake_send_cmd
    try:
        return fn()
    finally:
        r.send_cmd = real


def case_ack(r):
    # `ACK` が含まれていれば成功。EOT もエコーも本文の完全さも要求しない。
    check(r.ack_ok("ACK 115200 V-FAST\n"), "ack: plain ACK")
    check(r.ack_ok("> serial ack\nACK 9600 compat\n"), "ack: with echo")
    # 本文の一部が落ちても `ACK` さえ読めれば通す (往復 4 B2)。
    check(r.ack_ok("ACK 1152"), "ack: truncated body still counts")
    check(r.ack_ok("\xff\xfeACK 9600"), "ack: leading garbage tolerated")

    # `ACK` が無ければ失敗。
    check(not r.ack_ok(""), "ack: empty rejected")
    check(not r.ack_ok("> ver\nOS32 v1.4\n  Build: x\n"),
          "ack: ver reply is not an ack")
    check(not r.ack_ok("> serial ack\n"), "ack: echo alone is not an ack")

    check(r.ACK_CMD == "serial ack", "ack: command is 'serial ack'")
    check(r.ACK_EXPECT == "ACK", "ack: marker is 'ACK'")


def case_wait_ack(r):
    """**遅れた切替 EOT が先に来ても読み飛ばす** (往復 4 ⑤ の残り / B2)。"""
    calls = []

    # 1 回目は切替コマンドの遅れた応答 (EOT はあるが ACK が無い)。
    # 2 回目に本物の ACK。**最初の EOT で判定しない**ことが要。
    port = FakePort([
        ("> serial 115200\nRS-232C init: requested 115200bps\n", True),
        ("> serial ack\nACK 115200 V-FAST\n", True),
    ])
    ok, seen = with_fake_send(
        r, lambda: r.wait_ack(port, retry_s=0.01, total_s=1.0,
                              now=lambda: calls.append(1) or len(calls) * 0.001,
                              sleep=lambda _s: None))
    check(ok, "wait_ack: skips the delayed switch reply and finds ACK")
    check("ACK" in seen, "wait_ack: returns what it read")
    check(len(port.sent) == 2, "wait_ack: retried once")
    check(all(x.strip() == "serial ack" for x in port.sent),
          "wait_ack: always sends 'serial ack'")

    # 化けた行がいくつ来ても、ACK が来れば成功。
    # **投げ直しは間隔を空ける** — 応答が即返るとき (速度が合っていなくても
    # 前のコマンドの EOT がすぐ拾える) に間隔を空けないと、4 秒のあいだ
    # `serial ack` を浴びせ続けてゲストの入力を埋める。
    slept = []
    port = FakePort([("\xff\xfe", False), ("", False),
                     ("ACK 38400 V-FAST\n", True)])
    ok, _ = with_fake_send(
        r, lambda: r.wait_ack(port, retry_s=0.01, total_s=1.0,
                              now=lambda: 0.0, sleep=slept.append))
    check(ok, "wait_ack: garbage then ACK succeeds")
    check(len(slept) == 2, "wait_ack: paced once per retry")
    check(all(s > 0 for s in slept), "wait_ack: the pause is a real wait")

    # 1 回目で ACK が来たら投げ直さない。
    port = FakePort([("ACK 9600 compat\n", True)])
    ok, _ = with_fake_send(
        r, lambda: r.wait_ack(port, retry_s=0.01, total_s=1.0,
                              now=lambda: 0.0, sleep=lambda _s: None))
    check(ok, "wait_ack: first try succeeds")
    check(len(port.sent) == 1, "wait_ack: no needless retry")

    # ACK が一度も来なければ期限で諦める (**無限に投げ続けない**)。
    ticks = iter([0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0])
    port = FakePort([("> ver\n", True)] * 20)
    ok, _ = with_fake_send(
        r, lambda: r.wait_ack(port, retry_s=0.01, total_s=1.0,
                              now=lambda: next(ticks, 99.0),
                              sleep=lambda _s: None))
    check(not ok, "wait_ack: gives up without ACK")
    check(len(port.sent) <= 5, "wait_ack: bounded number of tries")


def case_probe(r):
    """失敗後に旧速度で生存を確かめる `ver` の判定 (往復 3 ⑤⑥ のまま)。"""
    good = "> ver\nOS32 v1.4\n  Build: Sep 22 2026 12:00:00\n"

    check(r.probe_ok(good, True), "probe: real ver reply")
    check(not r.probe_ok(good, False), "probe: no EOT is a failure")
    check(not r.probe_ok("", True), "probe: bare EOT rejected")
    check(not r.probe_ok("> serial 115200\nRS-232C init\n", True),
          "probe: delayed switch reply rejected")
    check(not r.probe_ok("> serial 9600\n  Build: Sep 22 2026\n", True),
          "probe: right body but wrong echo rejected")
    check(not r.probe_ok("> ver\n\xff\xfe\n", True),
          "probe: right echo but no body rejected")
    check(r.PROBE_EXPECT == "Build:", "probe: body marker is 'Build:'")


# ---------------------------------------------------------------------------
#  (c) 旧速度で待つ時間は番犬より短い (往復 4 B1)
# ---------------------------------------------------------------------------
def case_switch(r):
    # エコーは旧速度で読める (読めたら「応答は始まった」の手がかり)。
    check(r.switch_reply_ok("> serial 115200\nRS-232C init: requested\n",
                            115200),
          "switch: echo at the old speed")
    check(not r.switch_reply_ok("> serial 38400\n", 115200),
          "switch: echo for another baud rejected")
    check(not r.switch_reply_ok("", 115200), "switch: silence rejected")

    # **ホストが新速度で話しかけ始めるまでの時間が、ゲストの番犬より短い。**
    # 番犬は 500 tick = 5 秒。0.5 + 5.0 = 5.5 秒だった往復 3 の形では、
    # ホストが `serial ack` を投げる前に番犬が戻していた (B1)。
    start_talking = r.SPEED_SWITCH_SETTLE_S + r.SWITCH_REPLY_S
    check(start_talking <= 1.5, "switch: starts talking within 1.5s")
    check(start_talking < 5.0, "switch: starts talking before the watchdog")
    # ack を投げ直す窓も番犬の内側に収まる。
    check(start_talking + r.ACK_TOTAL_S <= 5.5,
          "switch: the whole ack window is about the watchdog span")
    check(r.ACK_RETRY_S <= 0.5, "switch: retries at least every 0.5s")

    # 失敗のあとゲストの番犬 (500 tick = 5 秒) が戻すのを待つ。
    check(r.WATCHDOG_WAIT_S > 5.0, "switch: waits past the guest watchdog")


# ---------------------------------------------------------------------------
#  (e) --fast で上げたら終わる前に --baud へ戻す (実機 2026-09-23)
# ---------------------------------------------------------------------------
def case_restore(r):
    # **switch_speed を逆向きに呼ぶ** (fast で `serial 9600` → 9600 で ack)。
    calls = []

    def fake_switch(port_name, open_baud, fast_baud, timeout_s):
        calls.append((port_name, open_baud, fast_baud))
        return "PORT", fast_baud, True, "linked at %d" % fast_baud

    real = r.switch_speed
    r.switch_speed = fake_switch
    try:
        port, baud, ok, note = r.restore_speed("COM3", 115200, 9600, 15.0)
    finally:
        r.switch_speed = real
    check(calls == [("COM3", 115200, 9600)],
          "restore: sends the revert at the fast speed, acks at --baud")
    check(ok and baud == 9600, "restore: reports the --baud speed")
    check("restored to 9600" in note, "restore: note names the speed")

    # 失敗は失敗と言う ([V4])。
    def fake_switch_fail(port_name, open_baud, fast_baud, timeout_s):
        return "PORT", open_baud, False, "fast switch failed, back at 115200"

    r.switch_speed = fake_switch_fail
    try:
        port, baud, ok, note = r.restore_speed("COM3", 115200, 9600, 15.0)
    finally:
        r.switch_speed = real
    check(not ok, "restore: failure is reported as failure")
    check("FAILED" in note and "115200" in note,
          "restore: failure note says the guest may still be fast")

    # main は cmd の後で戻す。--keep-fast だけが省く。
    src = inspect.getsource(r.main)
    check("--keep-fast" in src, "restore: --keep-fast flag exists")
    check("restore_speed(args.port, args.fast" in src,
          "restore: main calls restore_speed with the fast speed")
    check("restore_speed(args.port, args.fast" in inspect.getsource(r.run_mode),
          "restore: repl restores before sending 'exit'")


# ---------------------------------------------------------------------------
#  (d) --fast が受ける速度は資料の表と同じ
# ---------------------------------------------------------------------------
def case_bauds(r):
    check(r.FAST_BAUDS == (9600, 14400, 19200, 28800, 38400, 57600, 115200),
          "bauds: matches the V-FAST table in drivers/serial_plan.c")


CASES = {
    "echo": case_echo,
    "ack": case_ack,
    "wait_ack": case_wait_ack,
    "probe": case_probe,
    "switch": case_switch,
    "bauds": case_bauds,
    "restore": case_restore,
}

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
MUTATIONS = [
    (r"    return first == echo_line\(cmd\)",
     "    return first.startswith(echo_line(cmd))",
     "エコーを前方一致で見る (`> version` を `ver` の応答と読む — ⑥)"),
    (r"    if cmd in NO_ECHO_CMDS:\n        return True\n",
     "",
     "`exit` の例外を外す (エコーの無い `exit` を desync と誤判定 — ⑥)"),
    (r"    return ACK_EXPECT in text",
     "    return True",
     "ACK を見ずに何でも確認と読む (遅れた切替 EOT で成功にする — 往復 4)"),
    (r"        if ack_ok\(text\):\n            return True, \"\".join\(seen\)",
     "        if text:\n            return True, \"\".join(seen)",
     "最初の応答で判定する (遅れた切替 EOT を ACK と取り違える — 往復 4 ⑤)"),
    (r"        if now\(\) >= deadline:\n            return False, \"\".join\(seen\)",
     "        pass",
     "ACK が来なくても諦めない (投げ続けて戻ってこない)"),
    (r"        rest = step - \(now\(\) - started\)\n"
     r"        if rest > 0:\n            sleep\(rest\)",
     "        pass",
     "投げ直す間隔を空けない (応答が即返ると `serial ack` を浴びせ続けて"
     "ゲストの入力を埋める)"),
    (r"SWITCH_REPLY_S = 1\.0", "SWITCH_REPLY_S = 5.0",
     "旧速度で 5 秒待つ (0.5 + 5 = 5.5 秒 > 番犬の 5 秒 — 往復 4 B1)"),
    (r"ACK_TOTAL_S = 4\.0", "ACK_TOTAL_S = 30.0",
     "ack の窓を番犬よりずっと長くする (戻された後も投げ続ける)"),
    (r"    if PROBE_EXPECT not in text:\n        return False\n",
     "",
     "旧速度の生存確認で本文を見ない (先行の EOT を `ver` の成功と読む)"),
    (r"    port, baud, ok, note = switch_speed\(port_name, fast_baud, open_baud,",
     "    port, baud, ok, note = switch_speed(port_name, open_baud, fast_baud,",
     "戻しを --baud で送る (ゲストは --fast で聞いているので届かない)"),
    (r"        note = \"restore to %d FAILED: %s \(guest may still be at %d\)\" % \(\n"
     r"            open_baud, note, fast_baud\)",
     "        note = \"restored to %d\" % open_baud; ok = True",
     "戻せなくても restored と言う ([V4])"),
]


def run(mod, names):
    global FAILED
    bad = 0
    for name in names:
        FAILED = []
        CASES[name](mod)
        rc = 1 if FAILED else 0
        for f in FAILED:
            print(f"  FAIL {name}: {f}", flush=True)
        print(f"EXIT {name}={rc}", flush=True)
        bad += rc
    print(f"SUMMARY {len(names) - bad}/{len(names)} PASS", flush=True)
    return bad


def mutate(tmp):
    original = SRC.read_text(encoding="utf-8")
    bad = 0
    for i, (pattern, repl, why) in enumerate(MUTATIONS, 1):
        text, n = re.subn(pattern, repl, original, count=1)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        # **実物は書き換えない** — 写しの上で変異させる (check-par で並列可)。
        path = pathlib.Path(tmp) / ("mut%d.py" % i)
        path.write_text(text, encoding="utf-8")
        try:
            mod = load(path)
        except Exception:
            print(f"MUTATION {i} RED (import): {why}", flush=True)
            continue
        hits = 0
        for name in CASES:
            global FAILED
            FAILED = []
            try:
                CASES[name](mod)
            except (Exception, Runaway):
                FAILED.append("raised")
            hits += 1 if FAILED else 0
        status = "RED" if hits else "**GREEN (見逃し)**"
        print(f"MUTATION {i} {status} ({hits} 件): {why}", flush=True)
        bad += not hits
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    names = [a for a in args if not a.startswith("--")] or list(CASES)
    mod = load()
    print("HOST import PASS (real tools/rshell_serial.py)", flush=True)
    rc = run(mod, names)
    if "--mutate" in args:
        with tempfile.TemporaryDirectory(prefix="os32-rshell-serial-") as tmp:
            rc += mutate(tmp)
    sys.exit(bool(rc))
