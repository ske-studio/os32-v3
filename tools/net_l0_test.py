#!/usr/bin/env python3
"""OS32 リンク層 L0 (Stop-and-Wait) の検証 — docs/tasks/network/LINK_PLAN.md §5。

前提:
  1. tools/host_agent.py を NP2NETSOCK のポート (既定 127.0.0.1:8026) で listen 起動。
  2. その後 kernel-lgy98-link (LGY98_FLAG_LINKTEST) を配備してゲストを起動。
     起動時に drivers/lgy98.c が link_selftest(10) を実行し、HELLO のあと 10 回の
     PING/PONG 往復を行って link_* カウンタに残す。

この試験はカーネル内の link_* グローバルを /api/mem で読み、L0 の合格条件
(HELLO 確立・10/10 往復成功・再送や取りこぼしが妥当) を確認する。
"""
import json
import re
import struct
import sys
import urllib.request

BASE = "http://127.0.0.1:8025"
MAP = "build/out/kernel.map"
ROUNDS = 10


def http(path):
    with urllib.request.urlopen(BASE + path, timeout=10) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


def sym(name):
    for line in open(MAP, encoding="utf-8", errors="replace"):
        m = re.match(r"\s+0x([0-9a-f]+)\s+%s$" % re.escape(name), line)
        if m:
            return int(m.group(1), 16)
    return None


def mem(addr, n):
    return bytes.fromhex(http("/api/mem?addr=0x%x&len=%d&space=phys" % (addr, n))["hex"])


def u32(name):
    a = sym(name)
    if a is None:
        return None
    return struct.unpack("<I", mem(a, 4))[0]


def main():
    fails = 0
    net = http("/api/net")
    print("nic mac %s backend %s connected %s" % (net["mac"], net["backend"], net["connected"]))

    if sym("link_hello_ok") is None:
        print("RESULT: FAIL link_* not in kernel.map — build kernel-lgy98-link first")
        return 1

    hello = u32("link_hello_ok")
    # L0 専用のスナップショットを読む。link_rt_ok は自己試験の区間ごとに打ち直され、
    # 最終読み出し時には最後に走った L3 の値になっているため使えない (F2)。
    rt_ok = u32("link_l0_ok")
    rt_fail = u32("link_l0_fail")
    retx = u32("link_retransmits")
    rxf = u32("link_rx_frames")
    rxd = u32("link_rx_dropped")
    epoch = struct.unpack("<H", mem(sym("link_epoch"), 2))[0]
    pmac = mem(sym("link_peer_mac"), 6)
    print("link: hello_ok=%d peer=%s epoch=%d rt_ok=%d rt_fail=%d retransmit=%d rx_frames=%d rx_dropped=%d"
          % (hello, ":".join("%02x" % b for b in pmac), epoch, rt_ok, rt_fail, retx, rxf, rxd))

    checks = [
        ("HELLO established", hello == 1),
        ("peer MAC learned", any(pmac)),
        ("%d/%d round trips ok" % (rt_ok, ROUNDS), rt_ok == ROUNDS),
        ("no failed round trips", rt_fail == 0),
        ("rx frames >= round trips", rxf >= ROUNDS),
    ]
    for label, ok in checks:
        print("%-4s %s" % ("ok" if ok else "FAIL", label))
        fails += 0 if ok else 1

    cap = http("/api/net/capture")
    print("np21w capture: %d frames, net tx %s rx %s" % (len(cap.get("frames", [])), net["tx_frames"], net["rx_frames"]))
    print("RESULT: %s (%d failures)" % ("OK" if fails == 0 else "FAIL", fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
