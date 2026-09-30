#!/usr/bin/env python3
"""LGY-98 ドライバ M2 の対向試験 (docs/tasks/network/PLAN.md §6 / §8)。

前提: NP21/W (ai-debug) が LAN 有効 + 反射モード (make kernel-lgy98, FLAGS に
LGY98_FLAG_REFLECT) のカーネルで起動していること。対向機は不要で、
  POST /api/net/inject  でゲスト NIC に届けたフレームを、ドライバが受信して MAC を
  入れ替えて送り返し、GET /api/net/capture の dir=tx に現れることを照合する。
検査: 最短 / 奇数長 / 256 バイトページ境界前後 / 最大長 / 連続 10 フレーム。
最後に RESULT: OK / FAIL を 1 行出す (exit 0 / 1)。
"""
import argparse
import json
import re
import struct
import sys
import threading
import time
import urllib.parse
import urllib.request

BASE = "http://127.0.0.1:8025"
MAP = "build/out/kernel.map"
ETHERTYPE = 0x88B5          # IEEE 802.1 local experimental
PEER = bytes.fromhex("02005e000001")
LENS = [14, 60, 61, 100, 255, 256, 257, 1000, 1513, 1514]
BURST = 10


def http(path, body=None, timeout=15):
    req = urllib.request.Request(BASE + path, data=body, method="POST" if body is not None else "GET")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


def tvram_text():
    d = http("/api/tvram")
    return d.get("text") or "\n".join(d.get("lines", [])) if isinstance(d, dict) else str(d)


def send_key(seq):
    http("/api/key", ("seq=" + urllib.parse.quote(seq, safe="")).encode())


def sym(name):
    try:
        for line in open(MAP, encoding="utf-8", errors="replace"):
            m = re.match(r"\s+0x([0-9a-f]+)\s+%s$" % re.escape(name), line)
            if m:
                return int(m.group(1), 16)
    except OSError:
        pass
    return None


def read_u32(addr):
    d = http("/api/mem?addr=0x%x&len=4&space=phys" % addr)
    return struct.unpack("<I", bytes.fromhex(d["hex"]))[0]


# drivers/ne2000.c struct ne2k_dev のオフセット (i386-elf-gcc の offsetof で確認、2026-09-05)
NIC_IRQ_ON = 31
NIC_ST = 13716
ST_IRQ_COUNT, ST_IRQ_DEFERRED, ST_RX_DROPPED, ST_IRQ_RECHECKS, ST_BACKLOG = 20, 21, 11, 24, 25


def nic_snapshot():
    a = sym("ne2k_nic")
    if a is None:
        return None
    hdr = bytes.fromhex(http("/api/mem?addr=0x%x&len=40&space=phys" % a)["hex"])
    st = struct.unpack("<26I", bytes.fromhex(http("/api/mem?addr=0x%x&len=104&space=phys" % (a + NIC_ST))["hex"]))
    return {"irq_on": hdr[NIC_IRQ_ON], "irq_count": st[ST_IRQ_COUNT], "irq_deferred": st[ST_IRQ_DEFERRED],
            "rx_dropped": st[ST_RX_DROPPED], "irq_rechecks": st[ST_IRQ_RECHECKS], "backlog": st[ST_BACKLOG]}


def frame(mac, length, seq):
    payload = bytes((seq + i) & 0xFF for i in range(max(0, length - 14)))
    return mac + PEER + struct.pack(">H", ETHERTYPE) + payload


def expected_reflection(f):
    out = f[6:12] + f[0:6] + f[12:]
    if len(out) < 60:
        out += b"\0" * (60 - len(out))        # NP21/W は受信時に 60B へ padding する
    return out


def tx_frames():
    return [bytes.fromhex(x["hex"]) for x in http("/api/net/capture")["frames"] if x.get("dir") == "tx"]


def wait_tx(pred, timeout=3.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        for f in tx_frames():
            if pred(f):
                return f
        time.sleep(0.1)
    return None


class During:
    """試験の間、ゲストで別のコマンド (CPL3 プログラム) を常駐させておく。

    aidebug のデバッグサーバは単一スレッドで、要求を 1 つずつ処理する
    (aidebug_server.cpp の設計)。したがって sleep のように戻ってこない
    コマンドを /api/cmd で投げると、その間 inject / capture / mem がすべて
    詰まって試験にならない。ここでは less のような対話プログラムを使う:
    最初の画面を出してキー入力待ちに入ると /api/cmd は戻り (チャネルが空く)、
    プログラム自身は CPL3 に常駐したままになる。常駐は tvram の wait_for で確認し、
    終了は exit_key (less なら q) で促す。"""

    def __init__(self, cmd, exit_key, wait_for, settle, timeout):
        self.cmd, self.exit_key, self.wait_for = cmd, exit_key, wait_for
        self.settle, self.timeout = settle, timeout
        self.thread = None

    def _run(self):
        try:
            req = urllib.request.Request(BASE + "/api/cmd", data=self.cmd.encode(), method="POST")
            urllib.request.urlopen(req, timeout=self.timeout).read()
        except Exception:
            pass

    def start(self):
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()
        t0 = time.time()
        # /api/cmd が戻る (対話プログラムがキー待ちに入る) まではデバッグサーバが
        # 塞がっているので tvram も返らない。戻った後に wait_for を確認する。
        while time.time() - t0 < self.timeout:
            if not self.thread.is_alive():
                break
            time.sleep(0.5)
        else:
            print("FAIL during: '%s' never freed the debug channel within %ds "
                  "(non-interactive command? use an interactive one)" % (self.cmd, self.timeout))
            return False
        if self.wait_for and self.wait_for not in tvram_text():
            time.sleep(self.settle)
        if self.wait_for and self.wait_for not in tvram_text():
            print("FAIL during: '%s' is not resident ('%s' not on screen)" % (self.cmd, self.wait_for))
            return False
        print("during: '%s' resident at CPL3 while the LAN test runs" % self.cmd)
        return True

    def stop(self):
        if self.exit_key:
            send_key(self.exit_key)
            time.sleep(1.0)
        # ver でシェルに戻ったことを確認
        try:
            out = urllib.request.urlopen(urllib.request.Request(BASE + "/api/cmd", data=b"ver", method="POST"),
                                         timeout=60).read().decode("utf-8", "replace")
            ok = "OS32" in out
        except Exception:
            ok = False
        print("%-4s during: '%s' exited, shell responds (ver)" % ("ok" if ok else "FAIL", self.cmd))
        return ok

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--during-cmd", metavar="CMD", help="試験中にゲストで走らせておくシェルコマンド (CPL3 / V86)")
    ap.add_argument("--exit-key", metavar="SEQ", help="そのコマンドを終える /api/key の seq (例 q, CTRL+STOP)")
    ap.add_argument("--wait-for", metavar="TEXT", help="開始前に tvram に現れるのを待つ文字列 (例 A>)")
    ap.add_argument("--settle", type=float, default=2.0, help="開始後に待つ秒数")
    ap.add_argument("--during-timeout", type=int, default=180)
    args = ap.parse_args()

    fails = 0
    during = None
    if args.during_cmd:
        during = During(args.during_cmd, args.exit_key, args.wait_for, args.settle, args.during_timeout)
        if not during.start():
            print("RESULT: FAIL (could not start the concurrent guest command)")
            return 1

    net = http("/api/net")
    if not net.get("enabled"):
        print("RESULT: FAIL LAN is not enabled in NP21/W (/api/net enabled=false)")
        return 1
    mac = bytes.fromhex(net["mac"].replace(":", ""))
    print("nic mac %s io %s irq %s backend %s connected %s" %
          (net["mac"], net["io_base"], net["irq"], net["backend"], net["connected"]))

    refl_addr = sym("lgy98_reflected")
    fail_addr = sym("lgy98_reflect_fail")
    refl0 = read_u32(refl_addr) if refl_addr else None
    if refl0 is None:
        print("note: lgy98_reflected not in kernel.map, driver counters skipped")
    snap0 = nic_snapshot()

    # --- 長さ別 ---
    seq = 1
    for ln in LENS:
        http("/api/net/capture?clear=1")
        f = frame(mac, ln, seq)
        http("/api/net/inject", f.hex().encode())
        want = expected_reflection(f)
        got = wait_tx(lambda x: x[:12] == want[:12] and x[12:14] == want[12:14] and x[14:16] == want[14:16] if len(want) >= 16 else x[:12] == want[:12])
        ok = got is not None and got == want
        if got is not None and not ok:
            # 長さ違いか内容違いかを出す
            if len(got) != len(want):
                detail = "len got %d want %d" % (len(got), len(want))
            else:
                diff = next(i for i in range(len(got)) if got[i] != want[i])
                detail = "first diff at %d (got %02x want %02x)" % (diff, got[diff], want[diff])
        else:
            detail = "no reflection" if got is None else "match"
        print("%-4s len %4d -> %s" % ("ok" if ok else "FAIL", ln, detail))
        fails += 0 if ok else 1
        seq += 1

    # --- 連続 10 フレーム (反射は 100Hz tick で 2 フレーム/tick) ---
    http("/api/net/capture?clear=1")
    sent = []
    for i in range(BURST):
        f = frame(mac, 200 + i, 0x40 + i)
        sent.append(expected_reflection(f))
        http("/api/net/inject", f.hex().encode())
    t0 = time.time()
    while time.time() - t0 < 3.0:
        got = tx_frames()
        if len(got) >= BURST:
            break
        time.sleep(0.1)
    got = tx_frames()
    order_ok = got[:BURST] == sent
    print("%-4s burst %d -> got %d, in order %s" % ("ok" if order_ok else "FAIL", BURST, len(got), order_ok))
    fails += 0 if order_ok else 1

    # --- リング wrap: 1000〜1514B を長さを変えながら 60 フレーム逐次 (PSTOP を何度もまたぐ) ---
    # NIC リングは 0x46〜0xC0 (122 ページ)。1519B は 6 ページなので約 20 フレームごとに wrap する。
    wrap_n = 60
    wrap_fail = 0
    for i in range(wrap_n):
        ln = 1000 + (i * 37) % 515
        http("/api/net/capture?clear=1")
        f = frame(mac, ln, 0x80 + i)
        want = expected_reflection(f)
        http("/api/net/inject", f.hex().encode())
        got = wait_tx(lambda x: x[:14] == want[:14] and x[14:16] == want[14:16], 2.0)
        if got != want:
            wrap_fail += 1
            print("FAIL wrap #%d len %d -> %s" % (i, ln, "no reflection" if got is None else "content mismatch"))
    print("%-4s wrap %d frames (1000..1514B, ~%d PSTOP wraps) -> %d failures" %
          ("ok" if wrap_fail == 0 else "FAIL", wrap_n, wrap_n * 6 // 122, wrap_fail))
    fails += 0 if wrap_fail == 0 else 1

    if refl0 is not None:
        refl1 = read_u32(refl_addr)
        rfail = read_u32(fail_addr) if fail_addr else 0
        want_n = len(LENS) + BURST + wrap_n
        cnt_ok = (refl1 - refl0) == want_n and rfail == 0
        print("%-4s driver counters: reflected +%d (want %d), reflect_fail %d" %
              ("ok" if cnt_ok else "FAIL", refl1 - refl0, want_n, rfail))
        fails += 0 if cnt_ok else 1

    snap1 = nic_snapshot()
    if snap0 and snap1:
        d_irq = snap1["irq_count"] - snap0["irq_count"]
        print("driver: irq_on %d irq +%d deferred +%d rechecks +%d backlog_events +%d rx_dropped %d" %
              (snap1["irq_on"], d_irq, snap1["irq_deferred"] - snap0["irq_deferred"],
               snap1["irq_rechecks"] - snap0["irq_rechecks"], snap1["backlog"] - snap0["backlog"],
               snap1["rx_dropped"]))
        if snap1["irq_on"]:
            irq_ok = d_irq > 0 and snap1["rx_dropped"] == 0
            print("%-4s irq-driven: %d interrupts during test, host queue drops %d" %
                  ("ok" if irq_ok else "FAIL", d_irq, snap1["rx_dropped"]))
            fails += 0 if irq_ok else 1

    net = http("/api/net")
    print("np21w: tx %s rx %s tx_dropped %s rx_dropped %s" %
          (net["tx_frames"], net["rx_frames"], net["tx_dropped"], net["rx_dropped"]))
    if during is not None and not during.stop():
        fails += 1
    print("RESULT: %s (%d failures)" % ("OK" if fails == 0 else "FAIL", fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
