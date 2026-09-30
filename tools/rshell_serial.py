#!/usr/bin/env python3
"""rshell_serial.py — 実機の OS32 リモートシェル (rshell) にシリアルで話す。

ゲスト側 (userland/shell/rshell.c) の約束:
  - 9600bps 8N1 (drivers/serial.c の既定)。
  - ホストは 1 行 (コマンド + '\\n') を送り、ゲストは応答の最後に EOT (0x04) を返す。
  - rshell に入った瞬間にも EOT を 1 つ送る (同期用)。
NP21/W の aidebug が /api/cmd でやっていること (aidebug_api.cpp、timeout 15s) と同じ。

Windows 側の Python (pyserial 入り) で動かす:
  C:\\WATCOM\\.venv\\Scripts\\python.exe \\\\wsl.localhost\\Ubuntu\\home\\hight\\os32\\tools\\rshell_serial.py --port COM3 cmd ver
  ... --port COM3 repl          # 対話 (exit / Ctrl-C で終了。'exit' はゲストの rshell も閉じる)
  ... --port COM3 sync          # 溜まっている受信を捨てて EOT を待つだけ

--fast N で **繋いだあとに速度を上げる** (票 docs/archive/realhw_v21/TASK_SERIAL_VFAST.md):
  ... --port COM3 --fast 115200 cmd hexdump /bin/cfg.bin
ゲストは 9600 の互換モードで起動するので、
  1. --baud (既定 9600) で開いて `serial N` を送る
  2. 旧速度で応答を待つのは **1 秒だけ** — その行を書いている途中で速度が
     変わるので後半は必ず化ける。来なくても進む
  3. ポートを閉じて N で開き直し、**`serial ack` を 0.5 秒ごとに投げて
     `ACK` を含む応答が読めるまで最大 4 秒待つ** (他の応答は読み飛ばす)
  4. 読めれば `linked at N`、以後のコマンドは N で送る
  5. 読めなければ **ゲストの番犬が戻すのを 6 秒待ってから旧速度で `ver`**、
     `fast switch failed, back at 9600` と報告して終了コード 1
ゲスト側にも番犬があり、**切替後 5 秒 (500 tick) 以内に `serial ack` が
届かなければ自力で元へ戻す** (userland/shell/serial_watchdog.c)。だから
「FIFO 無し」「013Ah が効かない」「ケーブルが速度に耐えない」のどれでも
会話は 9600 で生き残る。N が --baud と同じなら切り替えは行わない。
  6. **終わる前に `serial <--baud>` を送って --baud で `serial ack` を待ち、
     `restored to 9600` と報告する** (repl は `exit` を送る前に戻す)。
     `--keep-fast` で戻さずに残せる — そのときは次の呼び出しを
     `--baud N` で開く。実機 (2026-09-23) で戻し忘れたまま 9600 で開いて
     3 回続けて化けた。

--serve-host DIR で **シリアル越しの /host** を出す (票 TASK_SERIAL_HOSTFS 部品 B):
  ... --port COM3 --fast 115200 --serve-host ./hostdrv cmd sfs run hsync boot
ゲストの `sfs run <コマンド行>` (常駐シェルの組込み) が SerialFS のセッションを
開き、`/host` に DIR をマウントしてコマンドを走らせる。ホストは **`sfs run` の
行を送ってから行末の EOT までだけ** フレームを解釈して答える (それ以外の行は
従来どおり — セッション外の `cat` の本文で要求が動かない)。セッション中の
ゲストの出力は溜められ、終わりに長さ付きのフレームで届く (`sfs: exit=N` を含む)。
終了コードはゲストの子の終了コードが 0 なら 0、それ以外は 1。
**ゲストからの書き込みは既定で禁止** — push の宛先などは `--allow-write <相対パス>`
(複数可) で明示する (票 B-7')。`--serve-host` 無しの `sfs run` は送らずに断る。
プロトコルは tools/serialfs_host.py と fs/sfs_proto.h。
NP21/W (ai-debug) では `--port aidebug:http://127.0.0.1:8025` で COM1 を HTTP の
/api/serial/read・write 越しに使える (ini は変えない。/api/cmd と同時に使わない)。
⚠ 本文に EOT (0x04) を含む出力はセッション外では途中で切れる (既知の制約)。

⚠ 確認は **明示の合図** で行う。往復 1〜3 は `ver` の応答を本文とエコーで
識別しようとして、遅れて届く EOT・本文の欠落・1 つずれた応答と穴が尽きなかった
(票 §4 の「設計変更 (往復 6)」)。
"""
import argparse
import os
import sys
import time

# pyserial はポートを開くときにしか要らない。**import 時に落とさない**のは、
# 応答の識別 (check_echo / probe_ok / switch_reply_ok) を pyserial 無しの
# ホストで試験できるようにするため (tools/tests/test_rshell_serial.py)。
try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    serial = None

EOT = b"\x04"

# SerialFS のホスト側 (同じディレクトリ)。pyserial と同じく import 時に
# 落とさない — 無くても従来の cmd / repl は動く。
try:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import serialfs_host
except ImportError:  # pragma: no cover
    serialfs_host = None

# `serial N` を送ってから閉じるまでの間合い [秒]。9600 で 14 文字 ≒ 15ms なので
# 十分な余裕。短くすると行の途中でポートを閉じてゲストが切り替えを始めない。
SPEED_SWITCH_SETTLE_S = 0.5
# --fast が受ける速度 (drivers/serial_plan.c の表と同じ。V･FAST に入れるのは
# FIFO 搭載機だけで、入れなければゲストは互換モードのまま = 速度が合わなくなる)。
FAST_BAUDS = (9600, 14400, 19200, 28800, 38400, 57600, 115200)
# ---------------------------------------------------------------------------
#  切替の確認は **専用の合図** で行う (Codex レビュー往復 4 で設計変更)
#
#  往復 1〜3 は `ver` の応答を本文とエコーで識別しようとしたが、遅れて届く
#  切替の EOT・本文の欠落・1 つずれた応答……と穴が尽きなかった。
#  いまはゲストに `serial ack` という専用の口があり、`ACK <baud> <mode>` を
#  返す。**何回投げてもよい (冪等)** ので、
#    - 0.5 秒ごとに投げ直す
#    - 応答に `ACK` が含まれるまで **他の応答は読み飛ばす**
#      (遅れた切替 EOT も、化けた行も、ここで自然に流れる)
#  という形にできる。最初の EOT では判定しない。
# ---------------------------------------------------------------------------
ACK_CMD = "serial ack"
ACK_EXPECT = "ACK"
# ack を投げ直す間隔と、諦めるまでの上限 [秒]。
ACK_RETRY_S = 0.5
ACK_TOTAL_S = 4.0
# 失敗したあと旧速度で生存を確かめるコマンド (本文で識別する)。
PROBE_CMD = "ver"
PROBE_EXPECT = "Build:"
# `serial N` の応答を **旧速度で** 待つ上限 [秒]。
# **短い。** 往復 3 では 5 秒待っていたが、0.5 + 5 = 5.5 秒はゲストの番犬
# (500 tick = 5 秒) を越えてしまい、**ホストが新速度で話しかける前に番犬が
# 戻す**ことがあった (往復 4 B1)。いまは遅れた切替 EOT を読み飛ばせる
# (ack を繰り返して `ACK` が見えるまで待つ) ので、ここで待ち切る必要が無い。
# 切替後にホストが新速度で `serial ack` を投げ始めるのは
# SPEED_SWITCH_SETTLE_S + SWITCH_REPLY_S = 最大 1.5 秒。番犬の 5 秒に余裕。
SWITCH_REPLY_S = 1.0
# エコーの無いコマンド。`exit` はゲストが rshell を閉じてから EOT を返すだけで
# `> exit` を出さない (userland/shell/rshell.c の `break` が kprintf より先)。
NO_ECHO_CMDS = ("exit",)
# 確認の往復に許す秒数。[V3] の 15 秒は「長いコマンド」の話で、ここは
# 「速度が合っているか」の判定 — 合っていれば 1 秒で返り、合っていなければ
# 何秒待っても返らない。5 秒はゲスト側の番犬 (500 tick) と同じ尺度。
SWITCH_PROBE_TIMEOUT_S = 5.0
# 失敗したあと、ゲストの番犬が元の速度へ戻すのを待つ秒数 (番犬は 5 秒 + 余裕)。
WATCHDOG_WAIT_S = 6.0


class AidebugPort(object):
    """NP21/W (ai-debug フォーク) の COM1 を HTTP で読み書きする口。

    `--port aidebug:http://127.0.0.1:8025` で使う。aidebug=true の NP21/W は COM1 を
    内部のリングにつなぎ、`GET /api/serial/read` (ゲスト → ホストの生バイト、16 進) と
    `POST /api/serial/write` (ホスト → ゲスト、16 進) を出している
    (np21w-src の aidebug_api.cpp)。**ini は変えない** ([D2])。pyserial の Serial の
    うち、この道具が使う read / write / flush / reset_input_buffer / close だけを持つ。
    ⚠ **NP21/W は通信速度を模擬しない** — ここで通っても実機の 115200 は別に確かめる。
    ⚠ /api/cmd と同じリングを読むので、同時に /api/cmd を使わないこと。
    """

    def __init__(self, url, timeout=0.2, urlopen=None):
        import urllib.request
        self.url = url.rstrip("/")
        self.timeout = timeout
        self.pending = bytearray()
        self._urlopen = urlopen or urllib.request.urlopen
        self._cancel = False

    def _read_once(self):
        import json
        with self._urlopen(self.url + "/api/serial/read", timeout=10) as r:
            obj = json.loads(r.read().decode("utf-8"))
        return bytes.fromhex(obj.get("hex", ""))

    @property
    def in_waiting(self):
        return len(self.pending)

    def read(self, n):
        """来ている分が無ければ timeout まで待つ (pyserial と同じ)。待ちは 10ms
        刻みで、cancel_read() が来たら空で戻る (受信スレッドを止めるため)。
        pending に残った分は捨てない (次の read が返す)。"""
        deadline = time.monotonic() + self.timeout
        while not self.pending and not self._cancel:
            self.pending += self._read_once()
            if self.pending or time.monotonic() >= deadline:
                break
            time.sleep(0.01)
        self._cancel = False
        out = bytes(self.pending[:n])
        del self.pending[:n]
        return out

    def cancel_read(self):
        """待っている read を空で戻す (pyserial の Serial.cancel_read と同じ名)。"""
        self._cancel = True

    def write(self, data):
        import urllib.request
        req = urllib.request.Request(self.url + "/api/serial/write",
                                     data=bytes(data).hex().encode("ascii"),
                                     method="POST")
        with self._urlopen(req, timeout=10) as r:
            r.read()
        return len(data)

    def flush(self):
        pass

    def reset_input_buffer(self):
        self.pending = bytearray()
        while self._read_once():
            pass

    def close(self):
        pass


AIDEBUG_PREFIX = "aidebug:"


def read_size(port):
    """来ている分 (in_waiting)、無ければ 1。serialfs_host.read_size と同じ規則
    (こちらは serialfs_host が無くても動くように持つ)。"""
    try:
        n = int(getattr(port, "in_waiting", 0) or 0)
    except Exception:  # noqa: BLE001
        n = 0
    return n if n > 0 else 1


def open_port(name, baud):
    if name.startswith(AIDEBUG_PREFIX):
        # NP21/W の COM1 (速度は模擬されないので baud は使わない)
        return AidebugPort(name[len(AIDEBUG_PREFIX):])
    return serial.Serial(name, baudrate=baud, bytesize=8, parity="N",
                         stopbits=1, timeout=0.2)


def read_until_eot(port, timeout_s):
    """EOT が来るまで読む。戻り値 (本文 bytes, EOT を見たか)。"""
    buf = bytearray()
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        # **来ている分だけ読む。** read(256) は 256 バイト揃うか timeout (0.2s)
        # まで戻らないので、短い応答ごとに 200ms 止まっていた (Fable M1)。
        chunk = port.read(read_size(port))
        if chunk:
            i = chunk.find(EOT)
            if i >= 0:
                buf += chunk[:i]
                return bytes(buf), True
            buf += chunk
    return bytes(buf), False


def send_cmd(port, line, timeout_s):
    port.reset_input_buffer()
    port.write(line.encode("utf-8") + b"\n")
    port.flush()
    body, ok = read_until_eot(port, timeout_s)
    return body.decode("utf-8", errors="replace"), ok


def echo_line(cmd):
    """ゲストが実行前に出すエコー行 (userland/shell/rshell.c)。"""
    return "> " + cmd


def check_echo(text, cmd):
    """応答が `> <送ったコマンド>` の **行全体** で始まっているか。

    `startswith` で前方一致だけを見ると `ver` の応答が `> version ...` でも
    通ってしまう (Codex レビュー往復 3 ⑥)。行末 (改行) まで込みで比べる。
    エコーを出さないコマンド (`exit`) は例外。
    """
    if cmd in NO_ECHO_CMDS:
        return True
    head = text.lstrip("\r\n")
    first = head.split("\n", 1)[0].rstrip("\r")
    return first == echo_line(cmd)


def ack_ok(text):
    """`serial ack` の応答として受け取ってよいか (往復 4)。

    見るのは **`ACK` が含まれること** だけ。EOT の対応も、エコー行も、
    本文の完全さも要求しない — どれも「たまたま落ちる」ことがあり、
    そのたびに穴になってきた。`serial ack` は冪等なので、**判定を緩くして
    投げ直す回数で確実さを稼ぐ**方が素直。
    """
    return ACK_EXPECT in text


def probe_ok(text, eot):
    """`ver` の応答として受け取ってよいか (往復 3 ⑤⑥)。

    失敗後に **旧速度で生存を確かめる**のに使う。3 つ揃って初めて成功:
      - EOT まで届いた
      - `ver` の本文にしか出ない `Build:` がある (先行コマンドの EOT を
        拾っただけなら本文が無い)
      - エコー行が `> ver` **ちょうど** (別のコマンドの応答ではない)
    """
    if not eot:
        return False
    if PROBE_EXPECT not in text:
        return False
    return check_echo(text, PROBE_CMD)


def switch_reply_ok(text, baud):
    """`serial N` の応答を旧速度で受け取れたか。

    エコー行 `> serial N` が読めれば、ゲストは切替行を受け取って実行に
    入っている。EOT は切替の途中で化けることがあるので **必須にしない** —
    エコーだけで「応答は始まった」と分かる。
    """
    return check_echo(text, "serial %d" % baud)


def probe(port, timeout_s):
    """実コマンド (`ver`) を投げて、**本文まで**返るか見る。

    失敗後に旧速度で生存を確かめるのに使う。こちらから 1 行送って往復が
    成立するかを見れば、速度が合っているかがそのまま分かる。
    """
    try:
        text, eot = send_cmd(port, PROBE_CMD, timeout_s)
    except Exception:            # pragma: no cover — 速度不一致で化けたとき
        return "", False
    return text, probe_ok(text, eot)


def wait_ack(port, retry_s=None, total_s=None, now=None, sleep=None):
    """`serial ack` を投げ直して `ACK` を含む応答を待つ (往復 4)。

    **最初の EOT では判定しない。** 遅れて届いた切替の EOT や化けた行は
    そのまま読み飛ばし、`ACK` が見えた時点で成功。`serial ack` は冪等なので
    何回投げてもよく、本文の一部が落ちても次の回で読める (往復 4 B2)。

    戻り値 (成功したか, 読めた本文の連結)。時計と sleep は試験のために
    差し替えられるようにしてある。
    """
    now = now or time.monotonic
    sleep = sleep or time.sleep
    deadline = now() + (ACK_TOTAL_S if total_s is None else total_s)
    step = ACK_RETRY_S if retry_s is None else retry_s
    seen = []
    while True:
        started = now()
        try:
            text, _ = send_cmd(port, ACK_CMD, step)
        except Exception:        # pragma: no cover — 速度不一致で化けたとき
            text = ""
        if text:
            seen.append(text)
        if ack_ok(text):
            return True, "".join(seen)
        if now() >= deadline:
            return False, "".join(seen)
        # **投げ直す間隔を守る。** 応答が即返ったとき (速度が合っていなくても
        # 前のコマンドの EOT がすぐ拾えることがある) に上の read が待たずに
        # 戻ると、ここが無ければ 4 秒間ひたすら `serial ack` を浴びせ続けて
        # **ゲストの入力を埋める**。1 回 / ACK_RETRY_S より速くは投げない。
        rest = step - (now() - started)
        if rest > 0:
            sleep(rest)


def switch_speed(port_name, open_baud, fast_baud, timeout_s):
    """ゲストを fast_baud へ切り替える。戻り値 (port, baud, ok, note)。

    **切替の応答は待ち切らない。** ゲストは `serial N` の途中で 013Ah を
    書き換えるので、そこから先のバイトは古い速度側では化ける。旧速度で
    見るのはエコー行だけ (1 秒)、来なくても新速度へ移る。

    新速度では **`serial ack` を 0.5 秒ごとに最大 4 秒**投げ、`ACK` を含む
    応答が読めたら成功 (遅れた切替 EOT や化けた行は読み飛ばす)。
    読めなければ元の速度へ開き直して `ver` を投げる。ゲスト側の番犬
    (userland/shell/serial_watchdog.c) が 5 秒で元へ戻しているはずなので、
    ここが通れば会話は生き残っている。
    """
    port = open_port(port_name, open_baud)
    try:
        port.reset_input_buffer()
        port.write(("serial %d\n" % fast_baud).encode("ascii"))
        port.flush()
        # 送信 FIFO が掃けて、ゲストが行を読み始めるまでの間合い。
        time.sleep(SPEED_SWITCH_SETTLE_S)
        # **旧速度で待つのは 1 秒だけ** (往復 4 B1)。来ても来なくても進む。
        # 遅れた切替 EOT は新速度側で `serial ack` を繰り返すあいだに
        # 読み飛ばせるので、ここで待ち切る必要が無い。長く待つと
        # ゲストの番犬 (5 秒) に間に合わなくなる。
        reply, _ = read_until_eot(port, SWITCH_REPLY_S)
        echoed = switch_reply_ok(reply.decode("utf-8", errors="replace"),
                                 fast_baud)
    finally:
        port.close()

    port = open_port(port_name, fast_baud)
    port.reset_input_buffer()
    # **新速度では `serial ack` を投げ直す** (往復 4)。ACK が見えたら成功。
    ok, _ = wait_ack(port)
    if ok:
        return port, fast_baud, True, "linked at %d" % fast_baud
    if not echoed:
        # 旧速度で `> serial N` すら見えていない = ゲストは切替行を
        # 受け取っていない可能性が高い。**それも報告に出す** ([V4])。
        note_extra = " (no '%s' echo at %d either)" % (
            echo_line("serial %d" % fast_baud), open_baud)
    else:
        note_extra = ""

    # 失敗。ゲストの番犬が元へ戻すのを待ってから、元の速度で確かめる。
    port.close()
    time.sleep(WATCHDOG_WAIT_S)
    port = open_port(port_name, open_baud)
    port.reset_input_buffer()
    _, back = probe(port, SWITCH_PROBE_TIMEOUT_S)
    note = ("fast switch failed, back at %d" % open_baud if back
            else "fast switch failed AND %d does not answer either"
                 % open_baud)
    return port, open_baud, False, note + note_extra


def restore_speed(port_name, fast_baud, open_baud, timeout_s):
    """`--fast` で上げたゲストを **終わる前に `--baud` へ戻す**。

    実機 (2026-09-23) で `--fast 115200 cmd ver` のあとゲストが 115200 に
    残り、次の 9600 の呼び出しが 3 回とも化けて timeout になった。切替は
    対称なので `switch_speed` を逆向きに使う: 115200 で `serial 9600` を
    送り、9600 で `serial ack` を待つ。戻り値は switch_speed と同じ
    (port, baud, ok, note)。失敗しても例外にしない — 結果は呼び手が
    [V4] のとおり表示する (ゲストの番犬が 5 秒で戻すはずだが、それも
    「戻った」とは言わない)。
    """
    port, baud, ok, note = switch_speed(port_name, fast_baud, open_baud,
                                        timeout_s)
    if ok:
        note = "restored to %d" % open_baud
    else:
        note = "restore to %d FAILED: %s (guest may still be at %d)" % (
            open_baud, note, fast_baud)
    return port, baud, ok, note


def main():
    # Windows のコンソール (cp932) でも化けた応答で落ちないようにする
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True,
                    help="COM3 / /dev/ttyUSB0 など。NP21/W の COM1 は "
                         "aidebug:http://127.0.0.1:8025 (HTTP の /api/serial/*)")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--timeout", type=float, default=15.0,
                    help="EOT を待つ秒数 ([V3]: 15 以上、長いコマンドは 60+)")
    ap.add_argument("--fast", type=int, default=0, metavar="N",
                    help="繋いだあと `serial N` でゲストを N bps へ上げてから"
                         " cmd / repl を回す (%s)"
                         % "/".join(str(b) for b in FAST_BAUDS))
    ap.add_argument("--keep-fast", action="store_true",
                    help="終わるときに --baud へ戻さず、ゲストを --fast の"
                         " 速度に残す (次の呼び出しは --baud N で開く)")
    ap.add_argument("--serve-host", metavar="DIR", default=None,
                    help="`sfs run ...` の行のあいだ DIR をゲストの /host として"
                         "シリアル越しに出す (票 TASK_SERIAL_HOSTFS 部品 B)")
    ap.add_argument("--allow-write", metavar="RELPATH", action="append",
                    default=[],
                    help="--serve-host の下でゲストが書いてよいパス (ルート相対、"
                         "その下も含む。複数可)。**既定は書き込み禁止** — "
                         "push の宛先はここに含める (票 B-7')")
    sub = ap.add_subparsers(dest="mode", required=True)
    p_cmd = sub.add_parser("cmd", help="1 コマンドを送って応答を出す")
    p_cmd.add_argument("line", nargs="+")
    sub.add_parser("repl", help="対話")
    sub.add_parser("sync", help="受信を捨てて EOT を 1 つ待つ")
    args = ap.parse_args()

    baud = args.baud
    switched = False
    if args.fast and args.fast != args.baud:
        if args.fast not in FAST_BAUDS:
            sys.stderr.write(
                "--fast は %s のいずれか (資料 io_rs.md の V･FAST 表)\n"
                % ", ".join(str(b) for b in FAST_BAUDS))
            return 2
        if args.mode == "sync":
            sys.stderr.write("--fast は cmd / repl で使う (sync は切り替えない)\n")
            return 2
        port, baud, ok, note = switch_speed(args.port, args.baud, args.fast,
                                            args.timeout)
        # **「切り替わった」と言い切らない** ([V4]) — `serial ack` が読めたか
        # だけを書く。失敗なら元の速度に戻っているので、そのまま終わる。
        print("[rshell_serial] %s" % note)
        if not ok:
            port.close()
            return 1
        switched = True
    else:
        port = open_port(args.port, baud)
    rc = 1
    try:
        rc, port, baud = run_mode(args, port, baud)
    finally:
        # **上げたら戻す。** ゲストを --fast に残すと、次の呼び出しが
        # --baud で開いて化ける (実機 2026-09-23)。repl の `exit` は
        # run_mode の中で先に戻してから送る (rshell が居なくなると
        # `serial N` を受ける相手が無い) ので、ここでは baud を見る。
        if switched and not args.keep_fast and baud == args.fast:
            port.close()
            port, baud, ok, note = restore_speed(args.port, args.fast,
                                                 args.baud, args.timeout)
            print("[rshell_serial] %s" % note)
            if not ok:
                rc = rc or 1
        port.close()
    return rc


def run_line(args, port, line, server=None, out=None):
    """1 行を送って応答を出す。戻り値 (終了コード, 応答の文字)。

    **`--serve-host` があり、行が `sfs run ...` のときだけ** SerialFS の
    フレームを解釈する (serve_line)。それ以外は従来どおり EOT まで読む
    だけで、本文に `ENQ 'S' 'F'` が並んでいても何も答えない。
    """
    out = out or sys.stdout
    is_sfs = (serialfs_host is not None and
              serialfs_host.sfs_child(line) is not None)
    if is_sfs and server is None:
        # 答える者が居ないのに送ると、ゲストは HELLO の期限 (約 10 秒) まで
        # 線を占有して断るだけ (Fable m4)。送らずに断る。
        print("[rshell_serial] 'sfs run' needs --serve-host DIR "
              "(not sent)")
        return 2, ""
    if is_sfs:
        r = serialfs_host.serve_line(port, line, server, args.timeout, out)
        text = r["text"].decode("utf-8", errors="replace")
        if not r["eot"]:
            print("\n[rshell_serial] timeout waiting for EOT (%.0fs without "
                  "progress)" % args.timeout)
            return 1, text
        if not check_echo(text, line):
            print("[rshell_serial] desync: expected echo of %r" % line)
            return 1, text
        if r["exit"] is None:
            print("[rshell_serial] sfs: session did not run (no EXIT frame)")
            return 1, text
        code, dropped, flags = r["exit"]
        print("[rshell_serial] sfs exit=%d sent=%d late=%d after_bye=%d "
              "bad_frames=%d%s%s"
              % (code, r["sent"], r["late"], r["after_bye"], r["bad_frames"],
                 " log_dropped=%d" % dropped if dropped else "",
                 " line_not_quiet" if flags & serialfs_host.XF_NOT_QUIET
                 else ""))
        return (0 if code == 0 else 1), text
    text, ok = send_cmd(port, line, args.timeout)
    out.write(text)
    if not ok:
        print("\n[rshell_serial] timeout waiting for EOT (%.0fs)" % args.timeout)
        return 1, text
    # **応答がこのコマンドのものか確かめる** (往復 2 B3)。EOT の対応が
    # 1 つずれていると、以後ずっと前のコマンドの応答を読み続ける。
    if not check_echo(text, line):
        print("[rshell_serial] desync: expected echo of %r" % line)
        return 1, text
    return 0, text


def make_server(args):
    if not args.serve_host:
        return None
    if serialfs_host is None:
        raise SystemExit("--serve-host には tools/serialfs_host.py が要る")
    fs = serialfs_host.HostFS(args.serve_host,
                              allow_write=args.allow_write or ())
    print("[serve-host] %s (%s, writable: %s)" % (
        args.serve_host, "fd-pinned" if fs.secure else "path-checked",
        ", ".join(args.allow_write) if args.allow_write else "none"))
    return serialfs_host.Server(fs, log=lambda m: print(m))


def run_mode(args, port, baud):
    """sync / cmd / repl の本体。戻り値 (終了コード, port, baud)。

    port は閉じない。repl の `exit` で速度を戻したときは新しい port と
    baud を返す (main の finally が二重に戻さないため)。
    """
    if args.mode == "sync":
        port.reset_input_buffer()
        body, ok = read_until_eot(port, args.timeout)
        sys.stdout.write(body.decode("utf-8", errors="replace"))
        print("\n[sync] EOT %s" % ("ok" if ok else "TIMEOUT"))
        return (0 if ok else 1), port, baud
    server = make_server(args)
    if args.mode == "cmd":
        line = " ".join(args.line)
        rc, _ = run_line(args, port, line, server)
        return rc, port, baud
    # repl
    print("[rshell_serial] %s %dbps — 'exit' でゲストの rshell も閉じる" % (args.port, baud))
    while True:
        try:
            line = input("os32> ")
        except (EOFError, KeyboardInterrupt):
            print()
            return 0, port, baud
        if not line.strip():
            continue
        if (line.strip() == "exit" and args.fast and baud == args.fast
                and args.fast != args.baud and not args.keep_fast):
            # rshell を閉じる前に速度を戻す (閉じた後では受け手が無い)。
            port.close()
            port, baud, ok, note = restore_speed(args.port, args.fast,
                                                 args.baud, args.timeout)
            print("[rshell_serial] %s" % note)
            if not ok:
                print("[rshell_serial] not sending 'exit' — guest speed unknown")
                return 1, port, baud
        # EOT の対応のずれ (往復 2 B3) と時間切れは run_line が表示する。
        # 対話は続けられるが **黙って進まない**。
        _, text = run_line(args, port, line, server)
        if not text.endswith("\n"):
            print()
        if line.strip() == "exit":
            return 0, port, baud

if __name__ == "__main__":
    sys.exit(main())
